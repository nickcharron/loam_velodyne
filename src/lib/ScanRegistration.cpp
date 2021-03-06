// Copyright 2013, Ji Zhang, Carnegie Mellon University
// Further contributions copyright (c) 2016, Southwest Research Institute
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from this
//    software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// This is an implementation of the algorithm described in the following paper:
//   J. Zhang and S. Singh. LOAM: Lidar Odometry and Mapping in Real-time.
//     Robotics: Science and Systems Conference (RSS). Berkeley, CA, July 2014.

#include "loam_velodyne/ScanRegistration.h"
#include "math_utils.h"

#include <tf/transform_datatypes.h>

namespace loam {

bool ScanRegistration::parseParams(const ros::NodeHandle &node,
                                   const ros::NodeHandle &privateNode,
                                       RegistrationParams &config_out) {
  bool success = true;
  int iParam = 0;
  bool bParam;
  std::string sParam;
  float fParam = 0;

  if (node.getParam("scanPeriod", fParam)) {
    if (fParam <= 0) {
      ROS_ERROR("Invalid scanPeriod parameter: %f (expected > 0)", fParam);
      success = false;
    } else {
      config_out.scanPeriod = fParam;
      ROS_DEBUG("Set scanPeriod: %g", fParam);
    }
  }

  if (privateNode.getParam("imuHistorySize", iParam)) {
    if (iParam < 1) {
      ROS_ERROR("Invalid imuHistorySize parameter: %d (expected >= 1)", iParam);
      success = false;
    } else {
      config_out.imuHistorySize = iParam;
      ROS_DEBUG("Set imuHistorySize: %d", iParam);
    }
  }

  if (privateNode.getParam("featureRegions", iParam)) {
    if (iParam < 1) {
      ROS_ERROR("Invalid featureRegions parameter: %d (expected >= 1)", iParam);
      success = false;
    } else {
      config_out.nFeatureRegions = iParam;
      ROS_DEBUG("Set nFeatureRegions: %d", iParam);
    }
  }

  if (privateNode.getParam("curvatureRegion", iParam)) {
    if (iParam < 1) {
      ROS_ERROR("Invalid curvatureRegion parameter: %d (expected >= 1)",
                iParam);
      success = false;
    } else {
      config_out.curvatureRegion = iParam;
      ROS_DEBUG("Set curvatureRegion: +/- %d", iParam);
    }
  }

  if (privateNode.getParam("maxCornerSharp", iParam)) {
    if (iParam < 1) {
      ROS_ERROR("Invalid maxCornerSharp parameter: %d (expected >= 1)", iParam);
      success = false;
    } else {
      config_out.maxCornerSharp = iParam;
      config_out.maxCornerLessSharp = 10 * iParam;
      ROS_DEBUG("Set maxCornerSharp / less sharp: %d / %d", iParam,
                config_out.maxCornerLessSharp);
    }
  }

  if (privateNode.getParam("maxCornerLessSharp", iParam)) {
    if (iParam < config_out.maxCornerSharp) {
      ROS_ERROR("Invalid maxCornerLessSharp parameter: %d (expected >= %d)",
                iParam, config_out.maxCornerSharp);
      success = false;
    } else {
      config_out.maxCornerLessSharp = iParam;
      ROS_DEBUG("Set maxCornerLessSharp: %d", iParam);
    }
  }

  if (privateNode.getParam("maxSurfaceFlat", iParam)) {
    if (iParam < 1) {
      ROS_ERROR("Invalid maxSurfaceFlat parameter: %d (expected >= 1)", iParam);
      success = false;
    } else {
      config_out.maxSurfaceFlat = iParam;
      ROS_DEBUG("Set maxSurfaceFlat: %d", iParam);
    }
  }

  if (privateNode.getParam("surfaceCurvatureThreshold", fParam)) {
    if (fParam < 0.001) {
      ROS_ERROR(
          "Invalid surfaceCurvatureThreshold parameter: %f (expected >= 0.001)",
          fParam);
      success = false;
    } else {
      config_out.surfaceCurvatureThreshold = fParam;
      ROS_DEBUG("Set surfaceCurvatureThreshold: %g", fParam);
    }
  }

  if (privateNode.getParam("lessFlatFilterSize", fParam)) {
    if (fParam < 0.001) {
      ROS_ERROR("Invalid lessFlatFilterSize parameter: %f (expected >= 0.001)",
                fParam);
      success = false;
    } else {
      config_out.lessFlatFilterSize = fParam;
      ROS_DEBUG("Set lessFlatFilterSize: %g", fParam);
    }
  }

  if (node.getParam("lidarFrame", sParam)) {
    _lidarFrame = sParam;
    ROS_DEBUG("Set lidar frame name to: %s", sParam.c_str());
  }

  if (node.getParam("imuFrame", sParam)) {
    _imuFrame = sParam;
    ROS_DEBUG("Set IMU frame name to: %s", sParam.c_str());
  }

  if (node.getParam("transformImuData", bParam)) {
    _transformIMU = bParam;
    ROS_DEBUG("Set transformImuData to: %d", bParam);
  }

  if (node.getParam("imuInputTopic", sParam)) {
    _imuInputTopic =sParam;
    ROS_DEBUG("Set IMU input topic name to: %s", sParam.c_str());
  }

  // Get transformation to apply to IMU
  if (_transformIMU) {
    tf2_ros::Buffer tfBuffer;
    tf2_ros::TransformListener tfListener(tfBuffer);
    bool transform_found = false;
    int counter = 0;
    while (!transform_found) {
      transform_found = true;
      counter++;
      try {
        _T_lidar_imu =
            tfBuffer.lookupTransform(_lidarFrame, _imuFrame, ros::Time(0));
        transform_found = true;
        ROS_INFO("Found IMU Lidar transform.");
      } catch (tf2::TransformException &ex) {
        transform_found = false;
        ROS_INFO("%s", ex.what());
        ROS_INFO("waiting for transform...");
        ros::Duration(1.0).sleep();
      }
      if (counter > 10) {
        ROS_ERROR("Cannot find transform from imu frame to lidar frame. Not "
                  "transforming data.");
        _transformIMU = false;
        transform_found = true;
      }
    }
  }

  return success;
}

bool ScanRegistration::setupROS(ros::NodeHandle &node,
                                ros::NodeHandle &privateNode,
                                RegistrationParams &config_out) {
  _transformIMU = false;
  _imuFrame = "/imu";
  _lidarFrame = "/camera";
  _imuInputTopic = "/imu/data";

  if (!parseParams(node, privateNode, config_out))
    return false;

  // subscribe to IMU topic
  _subImu = node.subscribe<sensor_msgs::Imu>(
      _imuInputTopic, 50, &ScanRegistration::handleIMUMessage, this);

  // advertise scan registration topics
  _pubLaserCloud =
      node.advertise<sensor_msgs::PointCloud2>("velodyne_cloud_2", 2);
  _pubCornerPointsSharp =
      node.advertise<sensor_msgs::PointCloud2>("laser_cloud_sharp", 2);
  _pubCornerPointsLessSharp =
      node.advertise<sensor_msgs::PointCloud2>("laser_cloud_less_sharp", 2);
  _pubSurfPointsFlat =
      node.advertise<sensor_msgs::PointCloud2>("laser_cloud_flat", 2);
  _pubSurfPointsLessFlat =
      node.advertise<sensor_msgs::PointCloud2>("laser_cloud_less_flat", 2);
  _pubImuTrans = node.advertise<sensor_msgs::PointCloud2>("imu_trans", 5);

  return true;
}

void ScanRegistration::handleIMUMessage(
    const sensor_msgs::Imu::ConstPtr &imuIn) {
  // rotate IMU data to lidar frame
  sensor_msgs::Imu::Ptr imuInRotated;
  if (_transformIMU) {
    imuInRotated = boost::make_shared<sensor_msgs::Imu>();
    transformIMU(*imuIn, *imuInRotated, _T_lidar_imu);
  } else {
    imuInRotated = boost::make_shared<sensor_msgs::Imu>(*imuIn);
  }

  // Output imu data:
  // std::cout << "IMU Rotated: \n" << *imuInRotated << "\n";

  tf::Quaternion orientation;
  tf::quaternionMsgToTF(imuInRotated->orientation, orientation);
  double roll, pitch, yaw;
  tf::Matrix3x3(orientation).getRPY(roll, pitch, yaw);

  Vector3 acc;
  acc.x() = float(imuInRotated->linear_acceleration.y -
                  sin(roll) * cos(pitch) * 9.81);
  acc.y() = float(imuInRotated->linear_acceleration.z -
                  cos(roll) * cos(pitch) * 9.81);
  acc.z() = float(imuInRotated->linear_acceleration.x + sin(pitch) * 9.81);

  IMUState newState;
  newState.stamp = fromROSTime(imuInRotated->header.stamp);
  newState.roll = roll;
  newState.pitch = pitch;
  newState.yaw = yaw;
  newState.acceleration = acc;

  updateIMUData(acc, newState);
}

void ScanRegistration::publishResult() {

  auto sweepStartTime = toROSTime(sweepStart());
  // publish full resolution and feature point clouds
  publishCloudMsg(_pubLaserCloud, laserCloud(), sweepStartTime, _lidarFrame);
  publishCloudMsg(_pubCornerPointsSharp, cornerPointsSharp(), sweepStartTime,
                  _lidarFrame);
  publishCloudMsg(_pubCornerPointsLessSharp, cornerPointsLessSharp(),
                  sweepStartTime, _lidarFrame);
  publishCloudMsg(_pubSurfPointsFlat, surfacePointsFlat(), sweepStartTime,
                  _lidarFrame);
  publishCloudMsg(_pubSurfPointsLessFlat, surfacePointsLessFlat(),
                  sweepStartTime, _lidarFrame);

  // publish corresponding IMU transformation information
  publishCloudMsg(_pubImuTrans, imuTransform(), sweepStartTime, _lidarFrame);
}

} // end namespace loam
