//==================================================
// sparse_reconstruction.h
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Dec 7, 2016
//==================================================
#pragma once

#include <unordered_map>
#include <bh/eigen.h>
#include <bh/eigen_serialization.h>
#include <bh/gps.h>
#include <bh/pose.h>

// Representations for a sparse reconstruction from Colmap.
// File input adapted from Colmap.
// Original copyright notice:
//
// COLMAP - Structure-from-Motion and Multi-View Stereo.
// Copyright (C) 2016  Johannes L. Schoenberger <jsch at inf.ethz.ch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <bh/vision/cameras.h>

namespace reconstruction {

using FloatType = float;
using GpsFloatType = double;
USE_FIXED_EIGEN_TYPES(FloatType)

using CameraMatrix = Eigen::Matrix<FloatType, 4, 4>;
using ColorVector = Eigen::Matrix<uint8_t, 3, 1>;

using CameraId = size_t;
using ImageId = size_t;
using Point3DId = size_t;
using FeatureId = size_t;

using Pose = bh::Pose<FloatType>;

class PinholeCamera {
public:
  static PinholeCamera createSimple(const size_t width, const size_t height, const FloatType focal_length);

  static PinholeCamera createSimple(const size_t width, const size_t height,
                                    const FloatType focal_length_x, const FloatType focal_length_y);

  PinholeCamera();

  PinholeCamera(size_t width, size_t height, const CameraMatrix& intrinsics);

  using bhPinholeCamera = bh::vision::PinholeCamera<FloatType>;

  operator bhPinholeCamera() const {
    return bhPinholeCamera(width(), height(), intrinsics());
  }

  bool operator==(const PinholeCamera& other) const;

  bool operator!=(const PinholeCamera& other) const;

  PinholeCamera getScaledCamera(const FloatType scale_factor) const;

  bool isValid() const;

  size_t width() const;

  size_t height() const;

  const CameraMatrix& intrinsics() const;

  Vector2 projectPoint(const Vector3& hom_point_camera) const;

  FloatType computeSizeOnSensorHorizontal(const FloatType size_x, const FloatType distance) const;

  FloatType computeSizeOnSensorVertical(const FloatType size_y, const FloatType distance) const;

  FloatType computeAreaOnSensor(const FloatType size_x, const FloatType size_y, const FloatType distance) const;

  FloatType computeRelativeSizeOnSensorHorizontal(const FloatType size_x, const FloatType distance) const;

  FloatType computeRelativeSizeOnSensorVertical(const FloatType size_y, const FloatType distance) const;

  FloatType computeRelativeAreaOnSensor(const FloatType size_x, const FloatType size_y, const FloatType distance) const;

  Vector3 getCameraRay(FloatType x, FloatType y) const;

  Vector3 getCameraRay(const Vector2& point_image) const;

  Vector3 unprojectPoint(FloatType x, FloatType y, FloatType distance) const;

  Vector3 unprojectPoint(const Vector2& point_image, FloatType distance) const;

  FloatType getMeanFocalLength() const;

  FloatType getFocalLengthX() const;

  FloatType getFocalLengthY() const;

  bool isPointInViewport(const Vector2& point) const;

  bool isPointInViewport(const Eigen::Vector2i& point) const;

  bool isPointInViewport(const Vector2& point, FloatType margin) const;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

private:
  // Boost serialization
  friend class boost::serialization::access;

  template <typename Archive>
  void serialize(Archive& ar, const unsigned int version) {
    ar & width_;
    ar & height_;
    ar & intrinsics_;
  }

  size_t width_;
  size_t height_;
  CameraMatrix intrinsics_;
};

class PinholeCameraColmap : public PinholeCamera {
public:
  PinholeCameraColmap(CameraId id, size_t width, size_t height, const std::vector<FloatType>& params);

  CameraId id() const;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

private:
  static CameraMatrix makeIntrinsicsFromParameters(const std::vector<FloatType>& params);

  CameraId id_;
};

struct Feature {
  Vector2 point;
  Point3DId point3d_id;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

struct ImageColmap {
public:
  ImageColmap(ImageId id, const Pose& pose, const std::string& name,
      const std::vector<Feature>& features, CameraId camera_id);

  ImageId id() const;
  const Pose& pose() const;
  const std::string& name() const;
  const std::vector<Feature>& features() const;
  const CameraId& camera_id() const;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

private:
  ImageId id_;
  Pose pose_;
  std::string name_;
  std::vector<Feature> features_;
  CameraId camera_id_;
};

struct Color : public ColorVector {
  uint8_t& r() { return (*this)(0); };
  uint8_t& g() { return (*this)(1); };
  uint8_t& b() { return (*this)(2); };
  const uint8_t& r() const { return (*this)(0); };
  const uint8_t& g() const { return (*this)(1); };
  const uint8_t& b() const { return (*this)(2); };

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

class Point3DStatistics {
public:
  Point3DStatistics();

  Point3DStatistics(FloatType average_distance, FloatType stddev_distance,
      FloatType stddev_one_minus_dot_product);

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW

  FloatType averageDistance() const;

  FloatType stddevDistance() const;

  FloatType stddevOneMinusDotProduct() const;

private:
  FloatType average_distance_;
  FloatType stddev_distance_;
  FloatType stddev_one_minus_dot_product_;
};

struct Point3D {

  struct TrackEntry {
    ImageId image_id;
    FeatureId feature_index;
  };

  Point3DId getId() const;

  const Vector3& getPosition() const;

  const Vector3& getNormal() const;

  const Point3DStatistics& getStatistics() const;

  Point3DId id;
  Vector3 pos;
  Color color;
  FloatType error;
  std::vector<TrackEntry> feature_track;
  Vector3 normal;
  Point3DStatistics statistics;

  EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};

const Point3DId invalid_point3d_id = std::numeric_limits<Point3DId>::max();

struct SfmToGpsTransformation {
  FloatType gps_scale;
  FloatType sfm_scale;
  FloatType gps_to_sfm_ratio;

  Vector3 gps_centroid;
  Vector3 sfm_centroid;
  Quaternion sfm_to_gps_quaternion;

  using GpsCoordinate = bh::GpsCoordinateWithAltitude<GpsFloatType>;
  GpsCoordinate gps_reference;
};

class SparseReconstruction {
public:
  using CameraMapType = EIGEN_ALIGNED_UNORDERED_MAP(CameraId, PinholeCameraColmap);
  using ImageMapType = EIGEN_ALIGNED_UNORDERED_MAP(ImageId, ImageColmap);
  using Point3DMapType = EIGEN_ALIGNED_UNORDERED_MAP(Point3DId, Point3D);

  SparseReconstruction();

  virtual ~SparseReconstruction();

  virtual void read(const std::string& path, const bool read_sfm_gps_transformation=false);

  const CameraMapType& getCameras() const;

  CameraMapType& getCameras();

  const ImageMapType& getImages() const;

  ImageMapType& getImages();

  const Point3DMapType& getPoints3D() const;

  Point3DMapType& getPoints3D();

  bool hasSfmGpsTransformation() const;

  const SfmToGpsTransformation& sfmGpsTransformation() const;

  SfmToGpsTransformation& sfmGpsTransformation();

private:
  void computePoint3DNormalAndStatistics(Point3D& point) const;

  void readCameras(std::string filename);

  void readCameras(std::istream& in);

  void readImages(std::string filename);

  void readImages(std::istream& in);

  void readPoints3D(std::string filename);

  void readPoints3D(std::istream& in);

  void readGpsTransformation(std::string filename);

  void readGpsTransformation(std::istream& in);

  CameraMapType cameras_;
  ImageMapType images_;
  Point3DMapType points3D_;
  bool has_sfm_gps_transformation_;
  SfmToGpsTransformation sfm_gps_transformation_;
};

}
