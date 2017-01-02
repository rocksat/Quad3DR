//==================================================
// octree_drawer.h
//
//  Copyright (c) 2016 Benjamin Hepp.
//  Author: Benjamin Hepp
//  Created on: Dec 7, 2016
//==================================================
#pragma once

#include <unordered_map>
#include <vector>
#include "../planner/viewpoint_planner_data.h"
#include "../planner/viewpoint_planner.h"
#include "triangle_drawer.h"
#include "voxel_drawer.h"

class OcTreeDrawer {
public:
  using FloatType = float;;

  OcTreeDrawer();
  virtual ~OcTreeDrawer();

  void draw(const QMatrix4x4& pvm_matrix, const QMatrix4x4& view_matrix, const QMatrix4x4& model_matrix);

  // initialization of drawer  -------------------------

  /// sets a new OcTree that should be drawn by this drawer
  void setOctree(const ViewpointPlanner::OccupancyMapType* octree) {
    octomap::pose6d o; // initialized to (0,0,0) , (0,0,0,1) by default
    setOctree(octree, o);
  }

  const std::vector<FloatType>& getOccupancyBins() const;

  /// sets a new OcTree that should be drawn by this drawer
  /// origin specifies a global transformation that should be applied
  virtual void setOctree(const ViewpointPlanner::OccupancyMapType* octree, const octomap::pose6d& origin);

  FloatType findOccupancyBin(FloatType occupancy) const;

  void updateVoxelsFromOctree();
  void updateVoxelData();
  void updateVoxelColorHeightmap();
  void updateRaycastVoxels(const std::vector<std::pair<ViewpointPlanner::ConstTreeNavigatorType, FloatType>>& raycast_voxels);
  void updateRaycastVoxels(const std::vector<std::pair<ViewpointPlannerData::OccupiedTreeType::IntersectionResult, FloatType>>& raycast_voxels);
  void updateRaycastVoxels(const std::vector<std::pair<const ViewpointPlanner::VoxelType*, FloatType>>& raycast_voxels);
  void updateRaycastVoxels(const ViewpointPlanner::VoxelWithInformationSet& raycast_voxels);
  void configVoxelDrawer(VoxelDrawer& voxel_drawer) const;

  FloatType getOccupancyBinThreshold() const;
  void setOccupancyBinThreshold(FloatType occupancy_bin_threshold);
  void setDrawOctree(bool draw_octree);
  void setDrawRaycast(bool draw_raycast);
  void setColorFlags(uint32_t color_flags_uint);
  void setDrawFreeVoxels(bool draw_free_voxels);
  void setDisplayAxes(bool display_axes);
  void setAlphaOccupied(FloatType alpha);
  void setDrawSingleBin(bool draw_single_bin);
  void setMinOccupancy(FloatType min_occupancy);
  void setMaxOccupancy(FloatType max_occupancy);
  void setMinObservations(uint32_t min_observations);
  void setMaxObservations(uint32_t max_observations);
  void setMinVoxelSize(FloatType min_voxel_size);
  void setMaxVoxelSize(FloatType max_voxel_size);
  void setMinWeight(FloatType min_weight);
  void setMaxWeight(FloatType max_weight);

  size_t getRenderTreeDepth() const;
  void setRenderTreeDepth(size_t render_tree_depth);
  size_t getRenderObservationThreshold() const;
  void setRenderObservationThreshold(size_t min_observations);

  // set new origin (move object)
  void setOrigin(octomap::pose6d t);

private:
  void drawVoxelsAboveThreshold(const QMatrix4x4& pvm_matrix, const QMatrix4x4& view_matrix, const QMatrix4x4& model_matrix,
      FloatType occupancy_threshold, bool draw_below_threshold=false);

  void setVertexDataFromOctomathVector(OGLVertexDataRGBA& vertex, const octomath::Vector3& vec);

  OGLColorData getVoxelColorData(const OGLVoxelData& voxel_data, FloatType min_z, FloatType max_z) const;

  void forEachVoxelDrawer(const std::function<void(VoxelDrawer&)> func);

  const ViewpointPlanner::OccupancyMapType* octree_;
  octomap::pose6d origin_;
  octomap::pose6d initial_origin_;

  std::vector<FloatType> occupancy_bins_;
  std::unordered_map<FloatType, VoxelDrawer> voxel_drawer_map_;
  bool draw_octree_;

  std::unique_ptr<VoxelDrawer> raycast_drawer_;
  bool draw_raycast_;

  FloatType occupancy_threshold_;
  VoxelDrawer::ColorFlags color_flags_;
  bool draw_free_voxels_;
  FloatType alpha_override_;
  bool draw_single_bin_;
  size_t render_tree_depth_;
  size_t render_observation_threshold_;

  FloatType min_occupancy_;
  FloatType max_occupancy_;
  uint32_t min_observations_;
  uint32_t max_observations_;
  uint32_t low_observation_count_;
  uint32_t high_observation_count_;
  FloatType min_voxel_size_;
  FloatType max_voxel_size_;
  FloatType min_weight_;
  FloatType max_weight_;
  FloatType low_weight_;
  FloatType high_weight_;
};