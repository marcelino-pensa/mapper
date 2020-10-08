/* Copyright (c) 2017, United States Government, as represented by the
 * Administrator of the National Aeronautics and Space Administration.
 * 
 * All rights reserved.
 * 
 * The Astrobee platform is licensed under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with the
 * License. You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#pragma once

#include <iostream>
#include <octomap/octomap.h>
#include <octomap/OcTree.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <sensor_msgs/point_cloud2_iterator.h>
#include <tf/transform_broadcaster.h>
#include <visualization_msgs/MarkerArray.h>

#include "mapper/graphs.h"
#include "mapper/helper.h"
#include "mapper/indexed_octree_key.h"
#include "mapper/linear_algebra.h"
#include "mapper/rrg.h"

#include <pensa_msgs/PathPlanningConfig.h>

#include <string>
#include <vector>

namespace octoclass {

// 3D occupancy grid
class OctoClass{
 public:
    octomap::OcTree tree_ = octomap::OcTree(0.1);  // create empty tree with resolution 0.1
    octomap::OcTree tree_inflated_ = octomap::OcTree(0.1);  // create empty tree with resolution 0.1
    double memory_time_;  // Fading memory of the tree in seconds
    algebra_3d::FrustumPlanes cam_frustum_;
    algebra_3d::PlanarLidar lidar_range_;

    // Constructor
    explicit OctoClass(const double &resolution,
                       const std::string &inertial_frame_id,  // Resolution in meters
                       const bool &map_3d);
    OctoClass();

    // Mapping methods
    void SetMemory(const double &memory);  // Fading memory time
    void SetMaxRange(const double &max_range);  // Max range for mapping
    void SetMinRange(const double &min_range);  // Min range for mapping
    void SetInertialFrame(const std::string &inertial_frame_id);  // Inertial frame id
    void SetResolution(const double &resolution_in);  // Resolution of the octomap
    void ResetMap();  // Reset the octomap structure
    void CopyMap(octomap::OcTree &tree, octomap::OcTree &tree_inflated);
    void SetMapInflation(const double &inflate_radius);  // Set the inflation radius (same for xyz)
    void SetMapInflation(const double &inflate_radius_xy,
                         const double &inflate_radius_z);  // Set inflation radius (xy and z different)
    void SetCamFrustum(const double &fov,
                       const double &aspect_ratio);
    void SetLidarRange(const double &min_range,
                       const double &max_range);
    void SetOccupancyThreshold(const double &occupancy_threshold);
    void SetHitMissProbabilities(const double &probability_hit,
                                 const double &probability_miss);
    void SetClampingThresholds(const double &clamping_threshold_min,
                               const double &clamping_threshold_max);
    void SetMap3d(const bool &map_3d);
    void SetPathPlanningConfig(const pensa_msgs::PathPlanningConfig &path_planning_config);
    std::string GetInertialFrameId() {return inertial_frame_id_;}
    void PointsOctomapToPointCloud2(const octomap::point3d_list& points,
                                    sensor_msgs::PointCloud2& cloud);  // Convert from octomap to pointcloud2
    void PclToRayOctomap(const pcl::PointCloud< pcl::PointXYZ > &cloud,
                          const tf::StampedTransform &tf_cam2world,
                          const algebra_3d::FrustumPlanes &frustum);    // Map obstacles and free area
    // Same as above, but without frustum filtering (for lidar)
    void PclToRayOctomap(const pcl::PointCloud< pcl::PointXYZ > &cloud,
                          const tf::StampedTransform &tf_cam2world);
    void ComputeUpdate(const octomap::KeySet &occ_inflated,  // Inflated endpoints
                       const octomap::KeySet &occ_slim,      // Non-inflated endpoints
                       const octomap::point3d& origin,
                       const double &maxrange,
                       octomap::KeySet *occ_slim_in_range,
                       octomap::KeySet *free_slim,
                       octomap::KeySet *free_inflated);  // Raycasting method for inflated maps
    void FadeMemory(const double &rate);  // Run fading memory method
    void InflateObstacles(const double &thickness);  // DEPRECATED: it was used to inflate the whole map (too expensive)
    // Returns all colliding nodes in the pcl
    void FindCollidingNodesTree(const pcl::PointCloud< pcl::PointXYZ > &point_cloud,
                                std::vector<octomap::point3d> *colliding_nodes);
    // Returns points from the pcl that collides with inflated tree
    void FindCollidingNodesInflated(const pcl::PointCloud< pcl::PointXYZ > &point_cloud,
                                    std::vector<octomap::point3d> *colliding_nodes);

    // Visualization methods
    void TreeVisMarkers(visualization_msgs::MarkerArray *obstacles,
                        visualization_msgs::MarkerArray *free);
    void InflatedVisMarkers(visualization_msgs::MarkerArray *obstacles,
                            visualization_msgs::MarkerArray *free);
    // void freeVisMarkers(visualization_msgs::MarkerArray* marker_array);
    // void inflatedFreeVisMarkers(visualization_msgs::MarkerArray* marker_array);

    // Useful methods
    // checkOccupancy functions: Returns -1 if node is unknown, 0 if its free and 1 if its occupied
    int CheckOccupancy(const octomap::point3d &p);  // Point collision
    int CheckOccupancy(const pcl::PointXYZ &p);  // Point collision
    int CheckOccupancy(const Eigen::Vector3d &p);  // Point collision
    int CheckOccupancy(const octomap::point3d &p1,
                       const octomap::point3d &p2);  // line collision
    int CheckOccupancy(const Eigen::Vector3d &p1,
                       const Eigen::Vector3d &p2);  // line collision

    // checkCollision functions: Returns 0 if it's free and 1 if its occupied or unknown
    bool CheckCollision(const octomap::point3d &p);  // Point collision
    bool CheckCollision(const Eigen::Vector3d &p);  // Point collision
    bool CheckCollision(const octomap::point3d &p1,
                        const octomap::point3d &p2);  // line collision
    bool CheckCollision(const Eigen::Vector3d &p1,
                        const Eigen::Vector3d &p2);  // line collision

    // Calculate the volume of free nodes in the bounding box
    void BBXFreeVolume(const Eigen::Vector3d &box_min,
                       const Eigen::Vector3d &box_max,
                       double *volume);
    // Calculate the volume of obstacles in the bounding box
    void BBXOccVolume(const Eigen::Vector3d &box_min,
                      const Eigen::Vector3d &box_max,
                      double *volume);

    // Return all free nodes within a bounding box
    void BBXFreeNodes(const Eigen::Vector3d &box_min,
                      const Eigen::Vector3d &box_max,
                      std::vector<octomap::OcTreeKey> *node_keys,
                      std::vector<double> *node_sizes);
    // Return all free nodes within a bounding box and assign them indexes
    void BBXFreeNodes(const Eigen::Vector3d &box_min,
                      const Eigen::Vector3d &box_max,
                      IndexedKeySet *indexed_node_keys,
                      std::vector<double> *node_sizes);

    // Return all occupied nodes within a bounding box (inflated tree)
    void OccNodesWithinBox(const Eigen::Vector3d &box_min,
                           const Eigen::Vector3d &box_max,
                           std::vector<Eigen::Vector3d> *node_center,
                           std::vector<double> *node_sizes);
    // Return all occupied nodes within a radius (inflated tree)
    void OccNodesWithinRadius(const geometry_msgs::Point &center_pt,
                              const double &radius,
                              std::vector<Eigen::Vector3d> *node_center);
    // Return nearest occupied node within a radius (inflated tree)
    bool NearestOccNodeWithinRadius(const geometry_msgs::Point &center_pt,
                                    const double &radius,
                                    Eigen::Vector3d *node_center,
                                    double *distance);

    // Find all immediate neighbors of a node
    void GetNodeNeighbors(const octomap::OcTreeKey &node_key,
                          const double &node_size,
                          std::vector<octomap::OcTreeKey> *neighbor_keys);  // Return all neighbors for a given node
    double GetNodeSize(const octomap::OcTreeKey &key);  // Returns size of node. Returns zero if node doesn't exist
    void PrintQueryInfo(octomap::point3d query,
                        octomap::OcTreeNode* node);

    // path planning methods (implementations in octopath.cc)
    void PathPruning(const std::vector<Eigen::Vector3d> &path,
                     const bool &free_space_only,
                     std::vector<Eigen::Vector3d> *compressed_path);
    bool OctoRRG(const Eigen::Vector3d &p0,
                 const Eigen::Vector3d &pf,
                 const Eigen::Vector3d &box_min,
                 const Eigen::Vector3d &box_max,
                 const double &max_time,
                 const int &max_nodes,
                 const double &steer_param,
                 const bool &free_space_only,
                 const bool &prune_result,
                 const bool &publish_rviz,
                 float *plan_time,
                 int *n_rrg_nodes,
                 std::vector<Eigen::Vector3d> *path,
                 visualization_msgs::Marker *graph_markers);
    void Astar(const octomap::point3d &p0,
               const octomap::point3d &pf,
               const bool &prune_result,
               float *plan_time,
               std::vector<Eigen::Vector3d> *path);

 private:
    int tree_depth_;
    double resolution_;
    double max_range_, min_range_;
    float inflate_radius_xy_, inflate_radius_z_;
    std::vector<Eigen::Vector3d> sphere_;  // Discretized sphere used in map inflation
    std::vector<double> depth_volumes_;     // Volume per depth in the tree
    std::string inertial_frame_id_;
    bool map_3d_;
    pensa_msgs::PathPlanningConfig path_planning_config_;

    // Methods
    double VectorNormSquared(const double &x,
                             const double &y,
                             const double &z);
    std_msgs::ColorRGBA HeightMapColor(const double &height,
                                       const double &alpha);
};

}  // namespace octoclass
