//==================================================
// sparse_stereo_zed.cpp
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Sep 2, 2016
//==================================================

#include <iostream>
#include <stdexcept>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <deque>
#include <condition_variable>
#include <tclap/CmdLine.h>
#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#if OPENCV_3
  //#include <opencv2/xfeatures2d.hpp>
//#include <opencv2/xfeatures2d/cuda.hpp>
  //#include <opencv2/sfm.hpp>
  #include <opencv2/cudafeatures2d.hpp>
  #include <opencv2/cudaimgproc.hpp>
#else
  #include <opencv2/gpu/gpu.hpp>
  #include <opencv2/gpu/gpumat.hpp>
#endif

#include <ait/video/video_source_zed.h>
#include <ait/stereo/sparse_stereo_matcher.h>
#include <ait/stereo/dense_stereo_matcher.h>
#include <ait/stereo/sparse_stereo.h>
#include <ait/utilities.h>
#include <ait/mLibInclude.h>


namespace ast = ait::stereo;

template <typename T>
struct LockedQueue
{
  std::mutex mutex;
  std::deque<T> queue;
};

template <typename T>
struct SparseStereoThreadData
{
  cv::Ptr<T> matcher_ptr;
  ast::StereoCameraCalibration calib;
  LockedQueue<ast::StereoAndDepthImageData> images_queue;
  std::condition_variable queue_filled_condition;
  bool stop = false;
  bool save_pointclouds = false;
};

template <typename T>
void runSparseStereoMatching(SparseStereoThreadData<T> &thread_data)
{
  const cv::Ptr<T> &matcher_ptr = thread_data.matcher_ptr;
  LockedQueue<ast::StereoAndDepthImageData> &images_queue = thread_data.images_queue;
  std::condition_variable &queue_filled_condition = thread_data.queue_filled_condition;
  bool &stop = thread_data.stop;

  int64_t start_ticks = cv::getTickCount();
  int frame_counter = 0;
  do
  {
    std::unique_lock<std::mutex> lock(images_queue.mutex);
    if (images_queue.queue.empty())
    {
      queue_filled_condition.wait_for(lock, std::chrono::milliseconds(100));
    }
    if (!images_queue.queue.empty())
    {
      ait::ProfilingTimer prof_timer;
      ast::StereoAndDepthImageData images(std::move(images_queue.queue.back()));
      images_queue.queue.pop_back();
      bool save_pointclouds = thread_data.save_pointclouds;
      lock.unlock();
      prof_timer.stopAndPrintTiming("Popping from queue and moving");
      ast::stereoMatchingTest(
          matcher_ptr, images, thread_data.calib, save_pointclouds);

      // Computing frame rate
      ++frame_counter;
      int64_t ticks = cv::getTickCount();
      double dt = double(ticks - start_ticks) / cv::getTickFrequency();
      double fps = frame_counter / dt;
      if (frame_counter % 10 == 0)
      {
//        std::cout << "Frame size: " << frame.cols << "x" << frame.rows << std::endl;
        std::cout << "Thread running with " << fps << std::endl;
        if (frame_counter > 30)
        {
          frame_counter = 0;
          start_ticks = ticks;
        }
      }

    }  // if (!images_queue.queue.empty())
  } while (!stop);
}

ait::stereo::CameraCalibration getCameraCalibrationFromZED(const sl::zed::CamParameters& params)
{
  ait::stereo::CameraCalibration calib;
  calib.camera_matrix = cv::Mat::zeros(3, 3, CV_64F);
  calib.camera_matrix.at<double>(0, 0) = params.fx;
  calib.camera_matrix.at<double>(1, 1) = params.fx;
  calib.camera_matrix.at<double>(2, 2) = 1;
  calib.camera_matrix.at<double>(0, 2) = params.cx;
  calib.camera_matrix.at<double>(1, 2) = params.cy;
  calib.dist_coeffs = cv::Mat::zeros(5, 1, CV_64F);
  // ZED SDK returns undistorted images
//  calib.dist_coeffs.at<double>(0, 0) = params.disto[0]; // k1
//  calib.dist_coeffs.at<double>(1, 0) = params.disto[1]; // k2
//  calib.dist_coeffs.at<double>(2, 0) = params.disto[3]; // r1 = p1
//  calib.dist_coeffs.at<double>(3, 0) = params.disto[4]; // r2 = p2
//  calib.dist_coeffs.at<double>(4, 0) = params.disto[2]; // k3
  return calib;
}

ait::stereo::StereoCameraCalibration getStereoCalibrationFromZED(sl::zed::Camera* zed)
{
  const sl::zed::StereoParameters* stereo_params = zed->getParameters();
  ait::stereo::StereoCameraCalibration calib;

  calib.image_size.width = zed->getImageSize().width;
  calib.image_size.height = zed->getImageSize().height;

  calib.left = getCameraCalibrationFromZED(stereo_params->LeftCam);
  calib.right = getCameraCalibrationFromZED(stereo_params->RightCam);

  calib.translation = cv::Mat::zeros(3, 1, CV_64F);
  calib.translation.at<double>(0, 0) = -stereo_params->baseline;
  calib.translation.at<double>(1, 0) = -stereo_params->Ty;
  calib.translation.at<double>(2, 0) = -stereo_params->Tz;

  cv::Mat rotVecX = cv::Mat::zeros(3, 1, CV_64F);
  cv::Mat rotVecY = cv::Mat::zeros(3, 1, CV_64F);
  cv::Mat rotVecZ = cv::Mat::zeros(3, 1, CV_64F);
  rotVecX = stereo_params->Rx;
  rotVecY = stereo_params->convergence;
  rotVecZ = stereo_params->Rz;
  cv::Mat rotX;
  cv::Mat rotY;
  cv::Mat rotZ;
  cv::Rodrigues(rotVecX, rotX);
  cv::Rodrigues(rotVecY, rotY);
  cv::Rodrigues(rotVecZ, rotZ);
  calib.rotation = rotX * rotY * rotZ;

  cv::Mat translation_cross = cv::Mat::zeros(3, 3, CV_64F);
  translation_cross.at<double>(1, 2) = -calib.translation.at<double>(0, 0);
  translation_cross.at<double>(2, 1) = +calib.translation.at<double>(0, 0);
  translation_cross.at<double>(0, 2) = +calib.translation.at<double>(1, 0);
  translation_cross.at<double>(2, 0) = -calib.translation.at<double>(1, 0);
  translation_cross.at<double>(0, 1) = -calib.translation.at<double>(2, 0);
  translation_cross.at<double>(1, 0) = +calib.translation.at<double>(2, 0);

  calib.essential_matrix = translation_cross * calib.rotation;
  calib.fundamental_matrix = calib.right.camera_matrix.t().inv() * calib.essential_matrix * calib.left.camera_matrix.inv();
  calib.fundamental_matrix /= calib.fundamental_matrix.at<double>(2, 2);

  std::cout << "width: " << calib.image_size.width << std::endl;
  std::cout << "height: " << calib.image_size.height << std::endl;
  std::cout << "translation: " << calib.translation << std::endl;
  std::cout << "rotation: " << calib.rotation << std::endl;
  std::cout << "left.camera_matrix: " << calib.left.camera_matrix << std::endl;
  std::cout << "left.dist_coeffs: " << calib.left.dist_coeffs << std::endl;
  std::cout << "right.camera_matrix: " << calib.right.camera_matrix << std::endl;
  std::cout << "right.dist_coeffs: " << calib.right.dist_coeffs << std::endl;
  std::cout << "essential_matrix: " << calib.essential_matrix << std::endl;
  std::cout << "fundamental_matrix: " << calib.fundamental_matrix << std::endl;

  calib.computeProjectionMatrices();

  return calib;
}

int main(int argc, char **argv)
{
  namespace avo = ait::video;

  try
  {
    TCLAP::CmdLine cmd("Sparse stereo matching ZED", ' ', "0.1");
    TCLAP::ValueArg<int> device_arg("d", "device", "Device number to use", false, 0, "id", cmd);
    TCLAP::ValueArg<std::string> video_arg("v", "video", "Video device file to use", false, "", "filename", cmd);
    TCLAP::ValueArg<std::string> svo_arg("", "svo", "SVO file to use", false, "", "filename", cmd);
    TCLAP::ValueArg<int> mode_arg("", "mode", "ZED Resolution mode", false, 2, "mode", cmd);
    TCLAP::ValueArg<double> fps_arg("", "fps", "Frame-rate to capture", false, 0, "Hz", cmd);
    TCLAP::SwitchArg hide_arg("", "hide", "Hide captured video", cmd, false);
    TCLAP::ValueArg<int> draw_period_arg("", "draw-period", "Period of drawing frames", false, 5, "integer", cmd);
    TCLAP::ValueArg<std::string> calib_arg("c", "calib", "Calibration file to use", false, "camera_calibration_stereo.yml", "filename", cmd);
    TCLAP::ValueArg<std::string> zed_params_arg("", "zed-params", "ZED parameter file", false, "", "filename", cmd);
    TCLAP::SwitchArg single_thread_arg("", "single-thread", "Use single thread", cmd, false);

    cmd.parse(argc, argv);

    // TODO
    cv_cuda::printCudaDeviceInfo(cv_cuda::getDevice());
//    cv_cuda::printShortCudaDeviceInfo(cv_cuda::getDevice());

    avo::VideoSourceZED video(sl::zed::STANDARD, true, true, true);

    if (zed_params_arg.isSet())
    {
      video.getInitParameters().load(zed_params_arg.getValue());
    }
//    video.getInitParameters().disableSelfCalib = false;
    if (svo_arg.isSet())
    {
      video.open(svo_arg.getValue());
    }
    else
    {
      video.open(static_cast<sl::zed::ZEDResolution_mode>(mode_arg.getValue()));
    }
    video.getInitParameters().save("MyParam");

    if (fps_arg.isSet())
    {
      if (video.setFPS(fps_arg.getValue()))
      {
        throw std::runtime_error("Unable to set ZED framerate");
      }
    }
    std::cout << "ZED framerate: " << video.getFPS() << std::endl;

    ast::StereoCameraCalibration calib = ast::StereoCameraCalibration::readStereoCalibration(calib_arg.getValue());
    // TODO
    calib = getStereoCalibrationFromZED(video.getNativeCamera());

#if OPENCV_2_4
    // FREAK
    using DetectorType = cv::FastFeatureDetector;
    using DescriptorType = cv::FREAK;
    cv::Ptr<DetectorType> detector = cv::makePtr<DetectorType>(20, true);
    cv::Ptr<DetectorType> detector_2 = cv::makePtr<DetectorType>(20, true);
//    cv::Ptr<DescriptorType> descriptor_computer = cv::makePtr<DescriptorType>(true, true, 22, 4);
//    cv::Ptr<DescriptorType> descriptor_computer_2 = cv::makePtr<DescriptorType>(true, true, 22, 4);
//    cv::Ptr<DescriptorType> descriptor_computer = cv::makePtr<DescriptorType>(true, true, 16, 3);
//    cv::Ptr<DescriptorType> descriptor_computer_2 = cv::makePtr<DescriptorType>(true, true, 16, 3);
    cv::Ptr<DescriptorType> descriptor_computer = cv::makePtr<DescriptorType>(true, true, 14, 2);
    cv::Ptr<DescriptorType> descriptor_computer_2 = cv::makePtr<DescriptorType>(true, true, 14, 2);

    // ORB
//    using DetectorType = cv::ORB;
//    using DescriptorType = cv::ORB;
//    cv::Ptr<DetectorType> detector = cv::makePtr<DetectorType>();
//    cv::Ptr<DescriptorType> descriptor_computer = cv::makePtr<DescriptorType>();

    // ORB CUDA
//    using FeatureType = cv::gpu::ORB_GPU;
//    cv::Ptr<FeatureType> feature_computer = cv::makePtr<FeatureType>();

    // Create feature detector
//    cv::Ptr<DetectorType> detector_2 = detector;
//    cv::Ptr<DescriptorType> descriptor_computer_2 = descriptor_computer;
    using FeatureDetectorType = ast::FeatureDetectorOpenCV<DetectorType, DescriptorType>;
    cv::Ptr<FeatureDetectorType> feature_detector = cv::makePtr<FeatureDetectorType>(detector, detector_2, descriptor_computer, descriptor_computer_2);
//      using FeatureDetectorType = ast::FeatureDetectorOpenCVCuda<FeatureType>;
//      cv::Ptr<FeatureDetectorType> feature_detector = cv::makePtr<FeatureDetectorType>(feature_computer);

    // Create sparse matcher
    using SparseStereoMatcherType = ast::SparseStereoMatcher<FeatureDetectorType>;
    SparseStereoMatcherType matcher(feature_detector, calib);

    // ORB and FREAK
    //    matcher.setFlannIndexParams(cv::makePtr<cv::flann::LshIndexParams>(20, 10, 2));
    matcher.setMatchNorm(cv::NORM_HAMMING);
#else
    // SURF CUDA
//    using FeatureType = cv::cuda::SURF_CUDA;
//    cv::Ptr<FeatureType> feature_computer = cv::makePtr<FeatureType>();

    // SURF
    using DetectorType = cv::xfeatures2d::SURF;
    using DescriptorType = cv::xfeatures2d::SURF;
    const int hessian_threshold = 1000;
    cv::Ptr<DescriptorType> detector = DetectorType::create(hessian_threshold);
    cv::Ptr<DescriptorType> descriptor_computer = detector;

//    // ORB
//    using DetectorType = cv::ORB;
//    using DescriptorType = cv::ORB;
//    const int num_features = 5000;
//    cv::Ptr<DetectorType> detector = DetectorType::create(num_features);
//    cv::Ptr<DescriptorType> descriptor_computer = detector;

    // ORB CUDA
//    using FeatureType = cv::cuda::ORB;
//    cv::Ptr<FeatureType> feature_computer = FeatureType::create(500, 1.2f, 8);

    // FREAK
//    using DetectorType = cv::FastFeatureDetector;
//    using DescriptorType = cv::xfeatures2d::FREAK;
//    cv::Ptr<DetectorType> detector = DetectorType::create(20, true);
//    cv::Ptr<DetectorType> detector_2 = DetectorType::create(20, true);
//    cv::Ptr<DescriptorType> descriptor_computer = DescriptorType::create();
//    cv::Ptr<DescriptorType> descriptor_computer_2 = DescriptorType::create();

    // Create feature detector
    cv::Ptr<DetectorType> detector_2 = detector;
    cv::Ptr<DescriptorType> descriptor_computer_2 = descriptor_computer;
    using FeatureDetectorType = ast::FeatureDetectorOpenCV<DetectorType, DescriptorType>;
    cv::Ptr<FeatureDetectorType> feature_detector = cv::makePtr<FeatureDetectorType>(detector, detector_2, descriptor_computer, descriptor_computer_2);
//    using FeatureDetectorType = ast::FeatureDetectorOpenCVSurfCuda<FeatureType>;
//    cv::Ptr<FeatureDetectorType> feature_detector = cv::makePtr<FeatureDetectorType>(feature_computer);
//      using FeatureDetectorType = ast::FeatureDetectorOpenCVCuda<FeatureType>;
//      cv::Ptr<FeatureDetectorType> feature_detector = cv::makePtr<FeatureDetectorType>(feature_computer);

    // Create sparse matcher
    using SparseStereoMatcherType = ast::SparseStereoMatcher<FeatureDetectorType>;
    cv::Ptr<SparseStereoMatcherType> matcher_ptr = cv::makePtr<SparseStereoMatcherType>(feature_detector, calib);

    // ORB
//    matcher_ptr->setFlannIndexParams(cv::makePtr<cv::flann::LshIndexParams>(20, 10, 2));
//    matcher_ptr->setMatchNorm(cv::NORM_HAMMING2);

    // FREAK
    //    matcher->setFlannIndexParams(cv::makePtr<cv::flann::LshIndexParams>(20, 10, 2));
//    matcher_ptr->setMatchNorm(cv::NORM_HAMMING);
#endif

    // General
//    feature_detector->setMaxNumOfKeypoints(500);
    matcher_ptr->setRatioTestThreshold(0.7);
    matcher_ptr->setEpipolarConstraintThreshold(5.0);

    SparseStereoThreadData<SparseStereoMatcherType> sparse_stereo_thread_data;
    sparse_stereo_thread_data.matcher_ptr = matcher_ptr;
    sparse_stereo_thread_data.calib = calib;

    std::thread sparse_matching_thread;
    if (!single_thread_arg.getValue())
    {
      sparse_matching_thread = std::thread([&] ()
      {
        runSparseStereoMatching(sparse_stereo_thread_data);
      });
    }

    int width = video.getWidth();
    int height = video.getHeight();

    cv::Size display_size(width, height);
    cv_cuda::GpuMat left_img_gpu;
    cv_cuda::GpuMat right_img_gpu;
    cv_cuda::GpuMat depth_img_gpu;
    cv_cuda::GpuMat depth_float_img_gpu;
    cv_cuda::GpuMat disparity_img_gpu;
    cv_cuda::GpuMat confidence_img_gpu;
    std::vector<cv::Point3d> pc_points;
    std::vector<cv::Point3d> pc_colors;

    bool opengl_supported = false;
    if (!hide_arg.getValue())
    {
      try
      {
        cv::namedWindow("left", cv::WINDOW_AUTOSIZE | cv::WINDOW_OPENGL);
        cv::namedWindow("right", cv::WINDOW_AUTOSIZE | cv::WINDOW_OPENGL);
        cv::namedWindow("depth", cv::WINDOW_AUTOSIZE | cv::WINDOW_OPENGL);
        opengl_supported = true;
      }
      catch (const cv::Exception &err)
      {
        cv::namedWindow("left", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("right", cv::WINDOW_AUTOSIZE);
        cv::namedWindow("depth", cv::WINDOW_AUTOSIZE);
      }
    }

    cv_cuda::Stream stream;
    int64_t start_ticks = cv::getTickCount();
    int frame_counter = 0;
    int key = -1;
    while (key != 27)
    {
      if (!video.grab())
      {
        throw std::runtime_error("Failed to grab frame from camera");
      }
      video.retrieveLeftGpu(&left_img_gpu, false);
      video.retrieveRightGpu(&right_img_gpu, false);
      video.retrieveDepthGpu(&depth_img_gpu, false);
      video.retrieveDepthFloatGpu(&depth_float_img_gpu, false);
      video.retrieveDisparityFloatGpu(&disparity_img_gpu, false);
      video.retrieveConfidenceFloatGpu(&confidence_img_gpu, false);
      // TODO
      video.retrievePointCloud(&pc_points, &pc_colors);
      sl::writePointCloudAs(video.getNativeCamera(), sl::POINT_CLOUD_FORMAT::PLY, "dense_zed.ply", true, false);

      if (!hide_arg.getValue() && frame_counter % draw_period_arg.getValue() == 0)
      {
        if (opengl_supported)
        {
          cv::imshow("left", left_img_gpu);
          cv::imshow("right", right_img_gpu);
//          cv::imshow("depth", depth_img_gpu);
          cv::Mat depth_img;
#if OPENCV_2_4
          stream.enqueueDownload(depth_img_gpu, depth_img);
#else
          depth_img_gpu.download(depth_img, stream);
#endif
          cv::extractChannel(depth_img, depth_img, 0);
          cv::imshow("depth", ait::Utilities::drawImageWithColormap(depth_img));
        }
        else
        {
          cv::Mat left_img;
          cv::Mat right_img;
          cv::Mat depth_img;
#if OPENCV_2_4
          stream.enqueueDownload(left_img_gpu, left_img);
          stream.enqueueDownload(right_img_gpu, right_img);
          stream.enqueueDownload(depth_img_gpu, depth_img);
#else
          left_img_gpu.download(left_img, stream);
          right_img_gpu.download(right_img, stream);
          depth_img_gpu.download(depth_img, stream);
#endif
          stream.waitForCompletion();
          cv::imshow("left", left_img);
          cv::imshow("right", right_img);
          cv::imshow("depth", depth_img);
        }
      }

      ait::ProfilingTimer timer;
      // Convert stereo image to grayscale
      cv_cuda::GpuMat left_img_grayscale_gpu;
      cv_cuda::GpuMat right_img_grayscale_gpu;
      if (left_img_gpu.channels() != 1 || right_img_gpu.channels() != 1)
      {
        timer = ait::ProfilingTimer();
        ait::Utilities::convertToGrayscaleGpu(left_img_gpu, &left_img_grayscale_gpu, stream);
        ait::Utilities::convertToGrayscaleGpu(right_img_gpu, &right_img_grayscale_gpu, stream);
        timer.stopAndPrintTiming("Converting images to grayscale");
      }
      else
      {
        left_img_grayscale_gpu = left_img_gpu;
        right_img_grayscale_gpu = right_img_gpu;
      }

      // Download stereo image to Host memory
      ast::StereoAndDepthImageData images(
          left_img_grayscale_gpu.rows, left_img_grayscale_gpu.cols,
          left_img_grayscale_gpu.type(),
          left_img_gpu.type(),
          depth_float_img_gpu.type());
      timer = ait::ProfilingTimer();
#if OPENCV_2_4
      stream.enqueueDownload(left_img_grayscale_gpu, images.left_img);
      stream.enqueueDownload(right_img_grayscale_gpu, images.right_img);
      stream.enqueueDownload(depth_float_img_gpu, images.depth_img);
      stream.enqueueDownload(left_img_gpu, images.left_img_color);
      stream.enqueueDownload(right_img_gpu, images.right_img_color);
#else
      left_img_grayscale_gpu.download(images.left_img, stream);
      right_img_grayscale_gpu.download(images.right_img, stream);
      depth_float_img_gpu.download(images.depth_img, stream);
      left_img_gpu.download(images.left_img_color, stream);
      right_img_gpu.download(images.right_img_color, stream);
#endif
      images.point_cloud_points = std::move(pc_points);
      images.point_cloud_colors = std::move(pc_colors);
      stream.waitForCompletion();
      timer.stopAndPrintTiming("Downloading images from GPU");
//
      // Push stereo image to queue and notify sparse matcher thread
      timer = ait::ProfilingTimer();
      {
        std::lock_guard<std::mutex> lock(sparse_stereo_thread_data.images_queue.mutex);
        sparse_stereo_thread_data.images_queue.queue.clear();
        sparse_stereo_thread_data.images_queue.queue.push_front(std::move(images));
      }
      timer.stopAndPrintTiming("Pushing to queue");

      if (single_thread_arg.getValue())
      {
        sparse_stereo_thread_data.stop = true;
        runSparseStereoMatching(sparse_stereo_thread_data);
      }
      else
      {
        sparse_stereo_thread_data.queue_filled_condition.notify_one();
      }

      // Computing frame rate
      ++frame_counter;
      int64_t ticks = cv::getTickCount();
      double dt = double(ticks - start_ticks) / cv::getTickFrequency();
      double fps = frame_counter / dt;
      if (frame_counter % 10 == 0)
      {
//        std::cout << "Frame size: " << frame.cols << "x" << frame.rows << std::endl;
        std::cout << "Running with " << fps << std::endl;
        if (frame_counter > 30)
        {
          frame_counter = 0;
          start_ticks = ticks;
        }
      }

      if (!hide_arg.getValue())
      {
        key = cv::waitKey(10) & 0xff;
        if (key == 'r')
        {
          std::cout << "Recording point clouds" << std::endl;
          sparse_stereo_thread_data.save_pointclouds = true;
        }
      }
    }  // while (key != 27)

    if (sparse_matching_thread.joinable())
    {
      sparse_stereo_thread_data.stop = true;
      sparse_matching_thread.join();
    }
  }
  catch (TCLAP::ArgException &err)
  {
    std::cerr << "Command line error: " << err.error() << " for arg " << err.argId() << std::endl;
  }

  return 0;
}
