// Copyright 2015-2020 TierIV.inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SIMULATION_API__API__API_HPP_
#define SIMULATION_API__API__API_HPP_

#include <simulation_api/entity/entity_manager.hpp>

#include <autoware_auto_msgs/msg/vehicle_control_command.hpp>
#include <autoware_auto_msgs/msg/vehicle_state_command.hpp>
#include <rclcpp/rclcpp.hpp>
#include <xmlrpcpp/XmlRpcClient.h>
#include <xmlrpcpp/XmlRpcValue.h>
#include <xmlrpcpp/XmlRpcException.h>

#include <memory>
#include <string>
#include <utility>

namespace scenario_simulator
{
class XmlRpcRuntimeError : public std::runtime_error
{
public:
  XmlRpcRuntimeError(const char * message, int res)
  : runtime_error(message), error_info_(res) {}

private:
  int error_info_;
};

class ExecutionFailedError : public std::runtime_error
{
public:
  explicit ExecutionFailedError(XmlRpc::XmlRpcValue value)
  : runtime_error(value["message"]) {}
  explicit ExecutionFailedError(const char * message)
  : runtime_error(message) {}
};

class API
{
  using EntityManager = simulation_api::entity::EntityManager;

public:
  template<class NodeT, class AllocatorT = std::allocator<void>>
  explicit API(
    NodeT && node, const std::string & map_path = "",
    const rclcpp::SubscriptionOptionsWithAllocator<AllocatorT> & options =
    rclcpp::SubscriptionOptionsWithAllocator<AllocatorT>())
  {
    std::string address = "127.0.0.1";

    int port = 8080;

    node->declare_parameter("port", port);
    node->get_parameter("port", port);
    node->undeclare_parameter("port");
    auto cmd_cb = std::bind(&API::vehicleControlCommandCallback, this, std::placeholders::_1);
    cmd_sub_ = rclcpp::create_subscription<autoware_auto_msgs::msg::VehicleControlCommand>(
      node,
      "input/vehicle_control_command",
      rclcpp::QoS(10),
      std::move(cmd_cb),
      options);
    auto state_cmd_cb = std::bind(&API::vehicleStateCommandCallback, this, std::placeholders::_1);
    state_cmd_sub_ = rclcpp::create_subscription<autoware_auto_msgs::msg::VehicleStateCommand>(
      node,
      "input/vehicle_state_command",
      rclcpp::QoS(10),
      std::move(state_cmd_cb),
      options);
    entity_manager_ptr_ = std::make_shared<EntityManager>(node, map_path);
    client_ptr_ =
      std::shared_ptr<XmlRpc::XmlRpcClient>(new XmlRpc::XmlRpcClient(address.c_str(), port));
  }

  bool spawn(
    bool is_ego, std::string name,
    std::string catalog_xml,
    simulation_api::entity::EntityStatus status);
  bool spawn(
    bool is_ego, std::string name,
    simulation_api::entity::VehicleParameters params,
    simulation_api::entity::EntityStatus status);
  bool spawn(
    bool is_ego, std::string name,
    simulation_api::entity::PedestrianParameters params,
    simulation_api::entity::EntityStatus status);
  bool spawn(
    bool is_ego, std::string name,
    std::string catalog_xml);
  bool spawn(
    bool is_ego, std::string name,
    simulation_api::entity::VehicleParameters params);
  bool spawn(
    bool is_ego, std::string name,
    simulation_api::entity::PedestrianParameters params);
  simulation_api::entity::EntityStatus getEntityStatus(
    std::string name,
    simulation_api::entity::CoordinateFrameTypes corrdinate =
    simulation_api::entity::CoordinateFrameTypes::LANE);
  bool setEntityStatus(std::string name, const simulation_api::entity::EntityStatus & status);
  boost::optional<double> getLongitudinalDistance(std::string from, std::string to);
  boost::optional<double> getTimeHeadway(std::string from, std::string to);
  void requestAcquirePosition(std::string name, std::int64_t lanelet_id, double s, double offset);
  void requestLaneChange(std::string name, std::int64_t to_lanelet_id);
  void requestLaneChange(std::string name, simulation_api::entity::Direction direction);
  bool isInLanelet(std::string name, std::int64_t lanelet_id, double tolerance);
  void setTargetSpeed(std::string name, double target_speed, bool continuous);
  geometry_msgs::msg::Pose getRelativePose(std::string from, std::string to);
  geometry_msgs::msg::Pose getRelativePose(geometry_msgs::msg::Pose from, std::string to);
  geometry_msgs::msg::Pose getRelativePose(std::string from, geometry_msgs::msg::Pose to);
  geometry_msgs::msg::Pose getRelativePose(
    geometry_msgs::msg::Pose from,
    geometry_msgs::msg::Pose to);
  bool reachPosition(std::string name, geometry_msgs::msg::Pose target_pose, double tolerance);
  bool reachPosition(
    std::string name, std::int64_t lanelet_id, double s, double offset, double tolerance);
  boost::optional<double> getStandStillDuration(std::string name) const;
  bool checkCollision(std::string name0, std::string name1);
  XmlRpc::XmlRpcValue initialize(
    double realtime_factor, double step_time, int times_try = 10,
    int duration_try_in_msec = 1000);
  XmlRpc::XmlRpcValue updateFrame();
  double getCurrentTime() const {return current_time_;}

private:
  std::shared_ptr<XmlRpc::XmlRpcClient> client_ptr_;
  std::shared_ptr<simulation_api::entity::EntityManager> entity_manager_ptr_;
  double step_time_;
  double current_time_;
  simulation_api::entity::EntityStatus toStatus(XmlRpc::XmlRpcValue param);
  XmlRpc::XmlRpcValue toValue(std::string name, simulation_api::entity::EntityStatus status);
  void vehicleControlCommandCallback(autoware_auto_msgs::msg::VehicleControlCommand::SharedPtr msg);
  boost::optional<autoware_auto_msgs::msg::VehicleControlCommand> current_cmd_;
  rclcpp::Subscription<autoware_auto_msgs::msg::VehicleControlCommand>::SharedPtr cmd_sub_;
  void vehicleStateCommandCallback(autoware_auto_msgs::msg::VehicleStateCommand::SharedPtr msg);
  boost::optional<autoware_auto_msgs::msg::VehicleStateCommand> current_state_cmd_;
  rclcpp::Subscription<autoware_auto_msgs::msg::VehicleStateCommand>::SharedPtr state_cmd_sub_;
};
}  // namespace scenario_simulator

#endif  // SIMULATION_API__API__API_HPP_