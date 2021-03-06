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

#include <mapper/mapper_class.h>
#include <mapper/helper.h>
#include <string>
#include <vector>
#include <algorithm>

namespace mapper {

// Thread for fading memory of the octomap
void MapperClass::FadeTask() {
    ROS_DEBUG("[mapper]: Fading Memory Thread started with rate %f: ", fading_memory_update_rate_);

    // Rate at which this thread will run
    ros::Rate loop_rate(1.1);

    while (!terminate_node_) {
        // Get time for when this task started
        const ros::Time t0 = ros::Time::now();

        mutexes_.octomap.lock();
            if (globals_.octomap.memory_time_ > 0) {
                globals_.octomap.FadeMemory(fading_memory_update_rate_);
            }
        mutexes_.octomap.unlock();

        // ros::Duration fade_time = ros::Time::now() - t0;
        // ROS_INFO("Fading memory execution time: %f", fade_time.toSec());

        loop_rate.sleep();
    }
    ROS_DEBUG("[mapper]: Exiting Fading Memory Thread...");
}

// Thread for updating the body tfTree values
void MapperClass::BodyTfTask(const std::string& parent_frame,
                             const std::string& child_frame) {
    ROS_DEBUG("[mapper]: robot frame tf Thread started with rate %f: ", tf_update_rate_);
    tf_listener::TfClass obj_body2world;
    ros::Rate loop_rate(tf_update_rate_);

    while (!terminate_node_) {
        // Get the transform
        obj_body2world.GetTransform(child_frame, parent_frame);
        // obj_body2world.PrintOrigin();

        mutexes_.body_tf.lock();
            globals_.tf_body2world = obj_body2world.transform_;
        mutexes_.body_tf.unlock();
        loop_rate.sleep();
    }

    ROS_DEBUG("[mapper]: Exiting body tf Thread...");
}

// Thread for updating the tfTree values
void MapperClass::CameraTfTask(const std::string& parent_frame,
                               const std::string& child_frame,
                               const uint& index) {
    ROS_DEBUG("tf Thread from frame `%s` to `%s` started with rate %f: ",
              child_frame.c_str(), parent_frame.c_str(), tf_update_rate_);
    tf_listener::TfClass obj_tf;
    ros::Rate loop_rate(tf_update_rate_);

    while (!terminate_node_) {
        // Get the transform
        obj_tf.GetTransform(child_frame, parent_frame);
        // obj_tf.PrintOrigin();

        mutexes_.cam_tf.lock();
            globals_.tf_cameras2world[index] = obj_tf.transform_;
        mutexes_.cam_tf.unlock();
        loop_rate.sleep();
    }

    ROS_DEBUG("[mapper]: Exiting camera tf Thread...");
}

void MapperClass::LidarTfTask(const std::string& parent_frame,
                              const std::string& child_frame,
                              const uint& index) {
    ROS_DEBUG("tf Thread from frame `%s` to `%s` started with rate %f: ",
              child_frame.c_str(), parent_frame.c_str(), tf_update_rate_);
    tf_listener::TfClass obj_tf;
    ros::Rate loop_rate(tf_update_rate_);

    while (!terminate_node_) {
        // Get the transform
        obj_tf.GetTransform(child_frame, parent_frame);
        // obj_tf.PrintOrigin();

        mutexes_.lidar_tf.lock();
            globals_.tf_lidar2world[index] = obj_tf.transform_;
        mutexes_.lidar_tf.unlock();
        loop_rate.sleep();
    }

    ROS_DEBUG("[mapper]: Exiting lidar tf Thread...");
}

void MapperClass::PathCollisionCheckTask() {
    ROS_DEBUG("[mapper]: collisionCheck Thread started!");

    // Rate at which the collision checker will run
    ros::Rate loop_rate(collision_check_rate_);

    // visualization markers
    visualization_msgs::MarkerArray traj_markers, samples_markers;
    visualization_msgs::MarkerArray compressed_samples_markers, collision_markers;

    // Drone's position variables
    geometry_msgs::Point robot_position, robot_projected_on_traj;

    // Variables for nearest collision
    geometry_msgs::Point nearest_collision;
    double collision_distance;

    // Size of trajectory markers
    mutexes_.sampled_traj.lock();
        const double traj_sample_size = globals_.sampled_traj.GetResolution();
    mutexes_.sampled_traj.unlock();
    double octomap_resolution;

    while (!terminate_node_) {
        // Get time for when this task started
        ros::Time time_now = ros::Time::now();

        // Copy trajectory into local point cloud and get visualization markers for path
        std::vector<octomap::point3d> colliding_nodes;
        mutexes_.sampled_traj.lock();
            const pcl::PointCloud<pcl::PointXYZ> point_cloud_traj = globals_.sampled_traj.point_cloud_traj_;
            globals_.sampled_traj.GetVisMarkers(&traj_markers, &samples_markers, &compressed_samples_markers);
        mutexes_.sampled_traj.unlock();

        // Stop execution if there are no points in the trajectory structure
        if (point_cloud_traj.size() <= 0) {
            visualization_functions::DrawCollidingNodes(colliding_nodes, inertial_frame_id_, 0.015, &collision_markers);
            this->PublishPathMarkers(collision_markers, traj_markers, samples_markers, compressed_samples_markers);
            continue;
        }

        // Get robot's current position
        robot_position = this->GetTfBodyToWorld();

        // Find point in trajectory that the robot is closest to
        if (!this->RobotPosProjectedOnTrajectory(robot_position, &robot_projected_on_traj)) {
            continue;
        }

        // Get drone's current set point
        geometry_msgs::Point current_set_point = this->GetCurrentSetPoint();

        // Set current trajectory to z=0 if working with 2D maps only
        if (!globals_.map_3d) {
            robot_position.z = 0.0;
            current_set_point.z = 0.0;
        }

        // Shift trajectory PCL so it passes along drone position
        Eigen::Vector3d vec_traj_to_drone;
        pcl::PointCloud<pcl::PointXYZ> point_cloud_traj_through_drone;
        helper::SubtractRosPoints(robot_projected_on_traj, robot_position, &vec_traj_to_drone);
        helper::ShiftPcl(point_cloud_traj, vec_traj_to_drone, &point_cloud_traj_through_drone);

        // Check if trajectory collides with points in the map
        this->GetCollidingNodesPcl(point_cloud_traj_through_drone, &colliding_nodes);
        // ROS_INFO("Colliding nodes: %d", int(colliding_nodes.size()) );

        // If there are collisions, find the nearest one and publish it
        if (colliding_nodes.size() > 0) {
            helper::FindNearestCollision(colliding_nodes, robot_position, &nearest_collision, &collision_distance);
            this->PublishNearestCollision(nearest_collision, collision_distance);
            ROS_DEBUG("[mapper]: Nearest collision in %.3f meters!", collision_distance);
        }

        // Visualization markers -------------------------------------------------------------------------------------
        // Visualization markers for shifted trajectory
        visualization_functions::TrajVisMarkers(point_cloud_traj_through_drone,
            inertial_frame_id_, traj_sample_size, &traj_markers);

        // Get visualization marker for current set point and robot position (projected and not projected)
        visualization_functions::ReferenceVisMarker(current_set_point, inertial_frame_id_, &traj_markers);
        visualization_functions::RobotPosVisMarker(robot_position, inertial_frame_id_, &traj_markers);
        visualization_functions::ProjectedPosVisMarker(robot_projected_on_traj, inertial_frame_id_, &traj_markers);

        // Draw colliding markers (delete if none)
        this->GetOctomapResolution(&octomap_resolution);
        visualization_functions::DrawCollidingNodes(colliding_nodes, inertial_frame_id_,
                                                    1.01*octomap_resolution, &collision_markers);

        // Publish all markers
        this->PublishPathMarkers(collision_markers, traj_markers, samples_markers, compressed_samples_markers);

        // ros::Duration solver_time = ros::Time::now() - time_now;
        // ROS_INFO("Collision check time: %f", solver_time.toSec());

        loop_rate.sleep();
    }

    ROS_DEBUG("[mapper]: Exiting collisionCheck Thread...");
}

void MapperClass::RadiusCollisionCheck() {
    ROS_DEBUG("[mapper]: RadiusCollisionCheck Thread started!");

    // Rate at which the collision checker will run
    ros::Rate loop_rate(collision_check_rate_);

    const double radius = radius_collision_check_;

    while (!terminate_node_) {
        loop_rate.sleep();

        // Get time for when this task started
        const ros::Time t0 = ros::Time::now();

        // Get robot's current position
        geometry_msgs::Point robot_position = this->GetTfBodyToWorld();

        // Set robot height to z=0 if working with 2D maps only
        if (!globals_.map_3d) {
            robot_position.z = 0.0;
        }

        // Convert robot position to Eigen
        Eigen::Vector3d center =
            msg_conversions::ros_point_to_eigen_vector(robot_position);

        // Publish visualization marker for radius
        this->PublishRadiusMarkers(center, radius);

        // get all occupied nodes within a radius
        Eigen::Vector3d box_min = center - Eigen::Vector3d(radius, radius, radius);
        Eigen::Vector3d box_max = center + Eigen::Vector3d(radius, radius, radius);
        Eigen::Vector3d nearest_node_center;
        double nearest_node_dist;
        mutexes_.octomap.lock();
            const bool there_are_nodes =
                globals_.octomap.NearestOccNodeWithinRadius(robot_position, radius,
                                                            &nearest_node_center, &nearest_node_dist);
        mutexes_.octomap.unlock();

        if (!there_are_nodes) {
            continue;
        }

        // Publish distance to obstacle in radius
        std_msgs::Float32 obstacle_msg;
        obstacle_msg.data = nearest_node_dist;
        obstacle_radius_pub_.publish(obstacle_msg);

        // ros::Duration radius_check_time = ros::Time::now() - t0;
        // ROS_INFO("RadiusCollisionCheck time: %f", radius_check_time.toSec());
    }
}

void MapperClass::OctomappingTask() {
    ROS_DEBUG("[mapper]: OctomappingTask Thread started!");
    tf::StampedTransform tf_cam2world;
    pcl::PointCloud< pcl::PointXYZ > pcl_world;

    uint semaphore_timeout = 1;  // seconds

    while (!terminate_node_) {
        // Wait until there is new pcl data
        struct timespec ts = helper::TimeFromNow(semaphore_timeout);
        if (sem_timedwait(&semaphores_.pcl, &ts) == -1) {
            // the point of this is to go back and check if (terminate_node_.GetValue() == true)
            continue;
        }

        // Get time for when this task started
        const ros::Time t0 = ros::Time::now();

        // Get Point Cloud
        mutexes_.point_cloud.lock();
            // Get data from queue
            pcl::PointCloud< pcl::PointXYZ > point_cloud = globals_.pcl_queue.front().cloud;
            const tf::StampedTransform tf_cam2world = globals_.pcl_queue.front().tf_cam2world;
            const bool is_lidar = globals_.pcl_queue.front().is_lidar;

            // Remove data from queue
            globals_.pcl_queue.pop();
        mutexes_.point_cloud.unlock();

        // Check if a tf message has been received already. If not, return
        if (tf_cam2world.stamp_.toSec() == 0) {
            continue;
        }

        // Get camera transform
        tf::Quaternion q = tf_cam2world.getRotation();
        tf::Vector3 v = tf_cam2world.getOrigin();
        Eigen::Affine3d transform = Eigen::Affine3d::Identity();
        transform.translation() << v.getX(), v.getY(), v.getZ();
        transform.rotate(Eigen::Quaterniond(q.getW(), q.getX(), q.getY(), q.getZ()));

        // Update frustum orientation
        algebra_3d::FrustumPlanes world_frustum;
        mutexes_.octomap.lock();
            globals_.octomap.cam_frustum_.TransformFrustum(transform, &world_frustum);
        mutexes_.octomap.unlock();

        // Should we process PCL data?
        mutexes_.update_map.lock();
            const bool update_map = globals_.update_map;
        mutexes_.update_map.unlock();

        // Process PCL data
        if (update_map) {
            // Transform pcl into world frame
            pcl::transformPointCloud(point_cloud, pcl_world, transform);

            // Save into octomap
            mutexes_.octomap.lock();
                if (is_lidar) {
                    globals_.octomap.PclToRayOctomap(pcl_world, tf_cam2world);
                } else {
                    globals_.octomap.PclToRayOctomap(pcl_world, tf_cam2world, world_frustum);
                }
                if (globals_.map_3d) {
                    globals_.octomap.tree_.prune();   // prune the tree before visualizing
                }
            mutexes_.octomap.unlock();
        }


        // Publish visualization markers iff at least one node is subscribed to it
        bool pub_obstacles, pub_free, pub_obstacles_inflated, pub_free_inflated;
        pub_obstacles = (obstacle_marker_pub_.getNumSubscribers() > 0);
        pub_free = (free_space_marker_pub_.getNumSubscribers() > 0);
        pub_obstacles_inflated = (inflated_obstacle_marker_pub_.getNumSubscribers() > 0);
        pub_free_inflated = (inflated_free_space_marker_pub_.getNumSubscribers() > 0);

        if (pub_obstacles || pub_free) {
            visualization_msgs::MarkerArray obstacle_markers;
            visualization_msgs::MarkerArray free_markers;
            mutexes_.octomap.lock();
                globals_.octomap.TreeVisMarkers(&obstacle_markers, &free_markers);
            mutexes_.octomap.unlock();
            if (pub_obstacles) {
                obstacle_marker_pub_.publish(obstacle_markers);
            }
            if (pub_free) {
                free_space_marker_pub_.publish(free_markers);
            }
        }

        if (pub_obstacles_inflated || pub_free_inflated) {
            visualization_msgs::MarkerArray inflated_markers;
            visualization_msgs::MarkerArray inflated_free_markers;
            mutexes_.octomap.lock();
                globals_.octomap.InflatedVisMarkers(&inflated_markers, &inflated_free_markers);
            mutexes_.octomap.unlock();
            if (pub_obstacles_inflated) {
                inflated_obstacle_marker_pub_.publish(inflated_markers);
            }
            if (pub_free_inflated) {
                inflated_free_space_marker_pub_.publish(inflated_free_markers);
            }
        }

        if ((!is_lidar) && (cam_frustum_pub_.getNumSubscribers() > 0)) {
            visualization_msgs::Marker frustum_markers;
            mutexes_.octomap.lock();
                globals_.octomap.cam_frustum_.VisualizeFrustum(point_cloud.header.frame_id, &frustum_markers);
            mutexes_.octomap.unlock();
            cam_frustum_pub_.publish(frustum_markers);
        } else if ((is_lidar) && (cam_frustum_pub_.getNumSubscribers() > 0)) {
            visualization_msgs::Marker lidar_range_marker;
            Eigen::Vector3d lidar_origin = transform.translation();
            if (!globals_.map_3d) {
                lidar_origin[2] = 0.0;
            }
            mutexes_.octomap.lock();
                globals_.octomap.lidar_range_.VisualizeRange(
                    globals_.octomap.GetInertialFrameId(), lidar_origin,
                    &lidar_range_marker);
            mutexes_.octomap.unlock();
            cam_frustum_pub_.publish(lidar_range_marker);
        }

        // // Notify the collision checker to check for collision due to map update
        // sem_post(&semaphores_.collision_check);

        // ros::Duration map_time = ros::Time::now() - t0;
        // ROS_INFO("Mapping time: %f", map_time.toSec());
    }

    ROS_DEBUG("[mapper]: Exiting OctomappingTask Thread...");
}

void MapperClass::KeyboardTask() {
    ROS_DEBUG("[mapper]: KeyboardTask Thread started!");

    std::string input_string;
    while (!terminate_node_) {
        std::getline(std::cin, input_string);
        ROS_INFO("Received command: %s", input_string.c_str());

        std::string filename1 = local_path_ + "/maps/octomap.ot";
        std::string filename2 = local_path_ + "/maps/octomap_inflated.ot";
        if (input_string == "save_map") {
            mutexes_.octomap.lock();
                globals_.octomap.tree_.write(filename1);
                globals_.octomap.tree_inflated_.write(filename2);
            mutexes_.octomap.unlock();

            ROS_INFO("Maps saved in:\n%s \n%s\n", filename1.c_str(), filename2.c_str());
        } else if (input_string == "load_map") {
            octomap::OcTree* tree = dynamic_cast<octomap::OcTree*>(octomap::OcTree::read(filename1));
            octomap::OcTree* tree_inflated = dynamic_cast<octomap::OcTree*>(octomap::OcTree::read(filename2));
            if (tree && tree_inflated) {
                mutexes_.octomap.lock();
                    globals_.octomap.CopyMap(*tree, *tree_inflated);
                mutexes_.octomap.unlock();
            }
        }
    }

    ROS_DEBUG("[mapper]: Exiting KeyboardTask Thread...");
}

void MapperClass::WaitForThreadsToEnd() {
    ROS_DEBUG("[mapper]: Wait for threads to return!");
    h_body_tf_thread_.join();
    h_octo_thread_.join();
    h_fade_thread_.join();
    h_collision_check_thread_.join();
    h_radius_collision_thread_.join();

    for (uint i = 0; i < h_cameras_tf_thread_.size(); i++) {
      h_cameras_tf_thread_[i].join();
    }
    for (uint i = 0; i < h_lidar_tf_thread_.size(); i++) {
      h_lidar_tf_thread_[i].join();
    }
    ROS_DEBUG("[mapper]: All threads have returned!");
}

}  // namespace mapper
