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

#include <simulation_api/color_utils/color_utils.hpp>
#include <simulation_api/hdmap_utils/hdmap_utils.hpp>
#include <simulation_api/math/hermite_curve.hpp>

#include <spline_interpolation/spline_interpolation.hpp>
#include <quaternion_operation/quaternion_operation.h>
#include <lanelet2_core/utility/Units.h>

#include <lanelet2_extension/io/autoware_osm_parser.hpp>
#include <lanelet2_extension/projection/mgrs_projector.hpp>
#include <lanelet2_extension/utility/message_conversion.hpp>
#include <lanelet2_extension/utility/utilities.hpp>
#include <lanelet2_extension/utility/query.hpp>
#include <lanelet2_extension/visualization/visualization.hpp>
#include <lanelet2_io/Io.h>
#include <lanelet2_io/io_handlers/Serialize.h>
#include <lanelet2_projection/UTM.h>

#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>
#include <boost/geometry/geometries/box.hpp>
#include <boost/assign/list_of.hpp>

#include <algorithm>
#include <vector>
#include <utility>
#include <memory>
#include <string>
#include <set>
#include <unordered_map>

namespace hdmap_utils
{
HdMapUtils::HdMapUtils(std::string lanelet_path, geographic_msgs::msg::GeoPoint origin)
{
  lanelet::GPSPoint origin_gps_point {origin.latitude, origin.longitude, origin.altitude};
  lanelet::Origin origin_lanelet {origin_gps_point};
  lanelet::projection::UtmProjector projector(origin_lanelet);
  lanelet::ErrorMessages errors;
  lanelet_map_ptr_ = lanelet::load(lanelet_path, projector, &errors);
  if (!errors.empty()) {
    for (const auto & error : errors) {
      std::cerr << error << std::endl;
    }
    throw HdMapError("failed to load lanelet map");
  }
  overwriteLaneletsCenterline();
  traffic_rules_vehicle_ptr_ =
    lanelet::traffic_rules::TrafficRulesFactory::create(lanelet::Locations::Germany,
      lanelet::Participants::Vehicle);
  vehicle_routing_graph_ptr_ = lanelet::routing::RoutingGraph::build(*lanelet_map_ptr_,
      *traffic_rules_vehicle_ptr_);
  traffic_rules_pedestrian_ptr_ =
    lanelet::traffic_rules::TrafficRulesFactory::create(lanelet::Locations::Germany,
      lanelet::Participants::Pedestrian);
  pedestrian_routing_graph_ptr_ = lanelet::routing::RoutingGraph::build(*lanelet_map_ptr_,
      *traffic_rules_pedestrian_ptr_);
  std::vector<lanelet::routing::RoutingGraphConstPtr> all_graphs;
  all_graphs.push_back(vehicle_routing_graph_ptr_);
  all_graphs.push_back(pedestrian_routing_graph_ptr_);
  overall_graphs_ptr_ = std::make_unique<lanelet::routing::RoutingGraphContainer>(all_graphs);
}

boost::optional<double> HdMapUtils::getCollisionPointInLaneCoordinate(
  std::int64_t lanelet_id,
  std::int64_t crossing_lanelet_id)
{
  namespace bg = boost::geometry;
  using Point = bg::model::d2::point_xy<double>;
  using Line = bg::model::linestring<Point>;
  using Polygon = bg::model::polygon<Point, false>;
  auto center_points = getCenterPoints(lanelet_id);
  std::vector<Point> path_collision_points;
  lanelet_map_ptr_->laneletLayer.get(crossing_lanelet_id);
  lanelet::CompoundPolygon3d lanelet_polygon =
    lanelet_map_ptr_->laneletLayer.get(crossing_lanelet_id).polygon3d();
  Polygon crosswalk_polygon;
  for (const auto & lanelet_point : lanelet_polygon) {
    crosswalk_polygon.outer().push_back(bg::make<Point>(lanelet_point.x(), lanelet_point.y()));
  }
  crosswalk_polygon.outer().push_back(crosswalk_polygon.outer().front());
  double s_in_lanelet = 0;
  for (size_t i = 0; i < center_points.size() - 1; ++i) {
    const auto p0 = center_points.at(i);
    const auto p1 = center_points.at(i + 1);
    const Line line{{p0.x, p0.y}, {p1.x, p1.y}};
    double line_length =
      std::sqrt(std::pow(p0.x - p1.x, 2) + std::pow(p0.y - p1.y, 2) + std::pow(p0.z - p1.z, 2));
    std::vector<Point> line_collision_points;
    bg::intersection(crosswalk_polygon, line, line_collision_points);
    if (line_collision_points.empty()) {
      continue;
    }
    std::vector<double> dist;
    for (size_t j = 0; j < line_collision_points.size(); ++j) {
      double s_in_line = 0;
      if (std::fabs(p1.x - p0.x) < DBL_EPSILON) {
        if (std::fabs(p1.y - p1.y < DBL_EPSILON)) {} else {
          s_in_line = (line_collision_points[j].y() - p0.y) / (p1.y - p0.y);
          return s_in_lanelet + s_in_line * line_length;
        }
      } else {
        s_in_line = (line_collision_points[j].x() - p0.x) / (p1.x - p0.x);
        return s_in_lanelet + s_in_line * line_length;
      }
    }
    s_in_lanelet = s_in_lanelet + line_length;
  }
  return boost::none;
}

std::vector<std::int64_t> HdMapUtils::getConflictingCrosswalkIds(
  std::vector<std::int64_t> lanelet_ids) const
{
  std::vector<std::int64_t> ret;
  for (const auto & lanelet_id : lanelet_ids) {
    const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
    const auto conflicting_crosswalks = overall_graphs_ptr_->conflictingInGraph(lanelet, 1);
    for (const auto & crosswalk : conflicting_crosswalks) {
      ret.emplace_back(crosswalk.id());
    }
  }
  return ret;
}

std::vector<geometry_msgs::msg::Point> HdMapUtils::clipTrajectoryFromLaneletIds(
  std::int64_t lanelet_id, double s,
  std::vector<std::int64_t> lanelet_ids, double foward_distance)
{
  std::vector<geometry_msgs::msg::Point> ret;
  bool on_traj = false;
  double rest_distance = foward_distance;
  for (auto id_itr = lanelet_ids.begin(); id_itr != lanelet_ids.end(); id_itr++) {
    double l = getLaneletLength(*id_itr);
    if (on_traj) {
      if (rest_distance < l) {
        for (double s_val = 0; s_val < rest_distance; s_val = s_val + 1.0) {
          auto map_pose = toMapPose(*id_itr, s_val, 0);
          if (map_pose) {
            ret.emplace_back(map_pose->pose.position);
          }
        }
        break;
      } else {
        rest_distance = rest_distance - l;
        for (double s_val = 0; s_val < l; s_val = s_val + 1.0) {
          auto map_pose = toMapPose(*id_itr, s_val, 0);
          if (map_pose) {
            ret.emplace_back(map_pose->pose.position);
          }
        }
        continue;
      }
    }
    if (lanelet_id == *id_itr) {
      on_traj = true;
      if ((s + foward_distance) < l) {
        for (double s_val = s; s_val < s + foward_distance; s_val = s_val + 1.0) {
          auto map_pose = toMapPose(lanelet_id, s_val, 0);
          if (map_pose) {
            ret.emplace_back(map_pose->pose.position);
          }
        }
        break;
      } else {
        rest_distance = rest_distance - (l - s);
        for (double s_val = s; s_val < l; s_val = s_val + 1.0) {
          auto map_pose = toMapPose(lanelet_id, s_val, 0);
          if (map_pose) {
            ret.emplace_back(map_pose->pose.position);
          }
        }
        continue;
      }
    }
  }
  return ret;
}

double HdMapUtils::getSpeedLimit(std::vector<std::int64_t> lanelet_ids)
{
  std::vector<double> limits;
  if (lanelet_ids.size() == 0) {
    throw HdMapError("size of the vector lanelet ids should be more than 1");
  }
  for (auto itr = lanelet_ids.begin(); itr != lanelet_ids.end(); itr++) {
    const auto lanelet = lanelet_map_ptr_->laneletLayer.get(*itr);
    const auto limit = traffic_rules_vehicle_ptr_->speedLimit(lanelet);
    limits.push_back(lanelet::units::KmHQuantity(limit.speedLimit).value() / 3.6);
  }
  return *std::min_element(limits.begin(), limits.end());
}

boost::optional<int> HdMapUtils::getLaneChangeableLenletId(
  std::int64_t lanelet_id,
  std::string direction)
{
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  if (direction == "left") {
    auto left_lanlet = vehicle_routing_graph_ptr_->left(lanelet);
    if (left_lanlet) {
      return left_lanlet->id();
    }
  }
  if (direction == "right") {
    auto right_lanlet = vehicle_routing_graph_ptr_->right(lanelet);
    if (right_lanlet) {
      return right_lanlet->id();
    }
  }
  return boost::none;
}

std::vector<std::int64_t> HdMapUtils::getPreviousLanelets(
  std::int64_t lanelet_id,
  double distance)
{
  std::vector<std::int64_t> ret;
  double total_dist = 0.0;
  ret.push_back(lanelet_id);
  while (total_dist < distance) {
    auto ids = getPreviousLaneletIds(lanelet_id, "straight");
    if (ids.size() != 0) {
      lanelet_id = ids[0];
      total_dist = total_dist + getLaneletLength(lanelet_id);
      ret.push_back(lanelet_id);
      continue;
    } else {
      auto else_ids = getPreviousLaneletIds(lanelet_id);
      if (else_ids.size() != 0) {
        lanelet_id = else_ids[0];
        total_dist = total_dist + getLaneletLength(lanelet_id);
        ret.push_back(lanelet_id);
        continue;
      } else {
        break;
      }
    }
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getFollowingLanelets(std::int64_t lanelet_id, double distance)
{
  std::vector<std::int64_t> ret;
  double total_dist = 0.0;
  ret.push_back(lanelet_id);
  while (total_dist < distance) {
    auto ids = getNextLaneletIds(lanelet_id, "straight");
    if (ids.size() != 0) {
      lanelet_id = ids[0];
      total_dist = total_dist + getLaneletLength(lanelet_id);
      ret.push_back(lanelet_id);
      continue;
    } else {
      auto else_ids = getNextLaneletIds(lanelet_id);
      if (else_ids.size() != 0) {
        lanelet_id = else_ids[0];
        total_dist = total_dist + getLaneletLength(lanelet_id);
        ret.push_back(lanelet_id);
        continue;
      } else {
        break;
      }
    }
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getRoute(
  std::int64_t from_lanelet_id,
  std::int64_t to_lanelet_id)
{
  std::vector<std::int64_t> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(from_lanelet_id);
  const auto to_lanelet = lanelet_map_ptr_->laneletLayer.get(to_lanelet_id);
  lanelet::Optional<lanelet::routing::Route> route = vehicle_routing_graph_ptr_->getRoute(lanelet,
      to_lanelet, 0,
      true);
  if (!route) {
    return ret;
  }
  lanelet::routing::LaneletPath shortest_path = route->shortestPath();
  if (shortest_path.empty()) {
    return ret;
  }
  for (auto lane_itr = shortest_path.begin(); lane_itr != shortest_path.end(); lane_itr++) {
    ret.push_back(lane_itr->id());
  }
  return ret;
}

std::vector<geometry_msgs::msg::Point> HdMapUtils::getCenterPoints(std::int64_t lanelet_id)
{
  std::vector<geometry_msgs::msg::Point> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto centerline = lanelet.centerline();
  for (const auto & point : centerline) {
    geometry_msgs::msg::Point p;
    p.x = point.x();
    p.y = point.y();
    p.z = point.z();
    ret.emplace_back(p);
  }
  return ret;
}

double HdMapUtils::getLaneletLength(std::int64_t lanelet_id) const
{
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto centerline = lanelet.centerline();
  double ret = 0;
  for (size_t i = 0; i < centerline.size() - 1; i++) {
    double x_diff = centerline[i].x() - centerline[i + 1].x();
    double y_diff = centerline[i].y() - centerline[i + 1].y();
    double z_diff = centerline[i].z() - centerline[i + 1].z();
    ret = ret + std::sqrt(x_diff * x_diff + y_diff * y_diff + z_diff * z_diff);
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getPreviousLaneletIds(std::int64_t lanelet_id) const
{
  std::vector<std::int64_t> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto previous_lanelets = vehicle_routing_graph_ptr_->previous(lanelet);
  for (const auto & llt : previous_lanelets) {
    ret.push_back(llt.id());
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getPreviousLaneletIds(
  std::int64_t lanelet_id,
  std::string turn_direction)
{
  std::vector<std::int64_t> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto previous_lanelets = vehicle_routing_graph_ptr_->previous(lanelet);
  for (const auto & llt : previous_lanelets) {
    const std::string turn_direction_llt = llt.attributeOr("turn_direction", "else");
    if (turn_direction_llt == turn_direction) {
      ret.push_back(llt.id());
    }
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getNextLaneletIds(std::int64_t lanelet_id) const
{
  std::vector<std::int64_t> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto following_lanelets = vehicle_routing_graph_ptr_->following(lanelet);
  for (const auto & llt : following_lanelets) {
    ret.push_back(llt.id());
  }
  return ret;
}

std::vector<std::int64_t> HdMapUtils::getNextLaneletIds(
  std::int64_t lanelet_id,
  std::string turn_direction)
{
  std::vector<std::int64_t> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto following_lanelets = vehicle_routing_graph_ptr_->following(lanelet);
  for (const auto & llt : following_lanelets) {
    const std::string turn_direction_llt = llt.attributeOr("turn_direction", "else");
    if (turn_direction_llt == turn_direction) {
      ret.push_back(llt.id());
    }
  }
  return ret;
}

double HdMapUtils::getTrajectoryLength(std::vector<geometry_msgs::msg::Point> trajectory)
{
  double ret = 0.0;
  for (size_t i = 0; i < trajectory.size() - 1; i++) {
    ret = ret + std::sqrt(std::pow(trajectory[i + 1].x - trajectory[i].x, 2) +
        std::pow(trajectory[i + 1].y - trajectory[i].y, 2) +
        std::pow(trajectory[i + 1].z - trajectory[i].z, 2));
  }
  return ret;
}

boost::optional<std::pair<simulation_api::math::HermiteCurve,
  double>> HdMapUtils::getLaneChangeTrajectory(
  geometry_msgs::msg::Pose from_pose,
  std::int64_t to_lanelet_id)
{
  double to_length = getLaneletLength(to_lanelet_id);
  std::vector<double> evaluation, target_s;
  std::vector<simulation_api::math::HermiteCurve> curves;

  for (double to_s = 0; to_s < to_length; to_s = to_s + 1.0) {
    auto goal_pose = toMapPose(to_lanelet_id, to_s, 0);
    if (goal_pose) {
      double start_to_goal_dist =
        std::sqrt(std::pow(from_pose.position.x - goal_pose->pose.position.x, 2) +
          std::pow(from_pose.position.y - goal_pose->pose.position.y, 2) +
          std::pow(from_pose.position.z - goal_pose->pose.position.z, 2));
      auto traj = getLaneChangeTrajectory(from_pose, to_lanelet_id, to_s, start_to_goal_dist * 0.5);
      if (traj) {
        if (traj->getMaximu2DCurvature() < 1.0) {
          double eval = std::fabs(40 - traj->getLength());
          evaluation.push_back(eval);
          curves.push_back(traj.get());
          target_s.push_back(to_s);
        }
      }
    }
  }
  if (evaluation.size() == 0) {
    return boost::none;
  }
  std::vector<double>::iterator min_itr = std::min_element(evaluation.begin(), evaluation.end());
  size_t min_index = std::distance(evaluation.begin(), min_itr);
  return std::make_pair(curves[min_index], target_s[min_index]);
}

boost::optional<simulation_api::math::HermiteCurve> HdMapUtils::getLaneChangeTrajectory(
  geometry_msgs::msg::Pose from_pose, std::int64_t to_lanelet_id, double to_s,
  double tangent_vector_size)
{
  std::vector<geometry_msgs::msg::Point> ret;
  auto to_vec = getTangentVector(to_lanelet_id, to_s);
  auto goal_pose = toMapPose(to_lanelet_id, to_s, 0);
  if (!to_vec || !goal_pose) {
    return boost::none;
  }
  geometry_msgs::msg::Vector3 start_vec = getVectorFromPose(from_pose, tangent_vector_size);
  geometry_msgs::msg::Vector3 goal_vec = to_vec.get();
  goal_vec.x = goal_vec.x * tangent_vector_size;
  goal_vec.y = goal_vec.y * tangent_vector_size;
  goal_vec.z = goal_vec.z * tangent_vector_size;
  simulation_api::math::HermiteCurve curve(from_pose, goal_pose->pose, start_vec, goal_vec);
  return curve;
}

geometry_msgs::msg::Vector3 HdMapUtils::getVectorFromPose(
  geometry_msgs::msg::Pose pose,
  double magnitude)
{
  geometry_msgs::msg::Vector3 dir =
    quaternion_operation::convertQuaternionToEulerAngle(pose.orientation);
  geometry_msgs::msg::Vector3 vector;
  vector.x = magnitude * std::cos(dir.z);
  vector.y = magnitude * std::sin(dir.z);
  vector.z = 0;
  return vector;
}

bool HdMapUtils::isInLanelet(std::int64_t lanelet_id, double s)
{
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto centerline = lanelet.centerline();
  std::vector<double> base_x = std::vector<double>(centerline.size());
  std::vector<double> base_y = std::vector<double>(centerline.size());
  std::vector<double> base_z = std::vector<double>(centerline.size());
  int point_index = 0;
  for (const auto & pt : centerline) {
    base_x[point_index] = pt.x();
    base_y[point_index] = pt.y();
    base_z[point_index] = pt.z();
    point_index++;
  }
  spline_interpolation::SplineInterpolator spline;
  std::vector<double> base_s = calcEuclidDist(base_x, base_y, base_z);
  std::vector<double> resampled_x;
  std::vector<double> resampled_y;
  std::vector<double> resampled_z;
  std::vector<double> resampled_s;
  double diff = 0.01;
  resampled_s.push_back(s);
  resampled_s.push_back(s + diff);
  if (
    !spline.interpolate(
      base_s, base_x, resampled_s, resampled_x, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_y, resampled_s, resampled_y, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_z, resampled_s, resampled_z, spline_interpolation::Method::SOR))
  {
    return false;
  }
  return true;
}

std::vector<geometry_msgs::msg::Point> HdMapUtils::toMapPoints(
  std::int64_t lanelet_id,
  std::vector<double> s)
{
  std::vector<geometry_msgs::msg::Point> ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto centerline = lanelet.centerline();
  std::vector<double> base_x;
  std::vector<double> base_y;
  std::vector<double> base_z;
  for (const auto & pt : centerline) {
    base_x.push_back(pt.x());
    base_y.push_back(pt.y());
    base_z.push_back(pt.z());
  }
  std::vector<double> base_s = calcEuclidDist(base_x, base_y, base_z);
  std::vector<double> resampled_x;
  std::vector<double> resampled_y;
  std::vector<double> resampled_z;
  spline_interpolation::SplineInterpolator spline;
  if (
    !spline.interpolate(
      base_s, base_x, s, resampled_x, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_y, s, resampled_y, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_z, s, resampled_z, spline_interpolation::Method::SOR))
  {
    return ret;
  }
  for (size_t i = 0; i < s.size(); i++) {
    geometry_msgs::msg::Point p;
    p.x = resampled_x[i];
    p.y = resampled_y[i];
    p.z = resampled_z[i];
    ret.push_back(p);
  }
  return ret;
}

boost::optional<geometry_msgs::msg::PoseStamped> HdMapUtils::toMapPose(
  simulation_api::entity::EntityStatus status)
{
  if (status.coordinate == simulation_api::entity::WORLD) {
    geometry_msgs::msg::PoseStamped ret;
    ret.header.frame_id = "map";
    ret.pose = status.pose;
    return ret;
  }
  if (status.coordinate == simulation_api::entity::LANE) {
    boost::optional<geometry_msgs::msg::PoseStamped> ret;
    ret = toMapPose(status.lanelet_id, status.s, status.offset, status.rpy);
    return ret;
  }
  return boost::none;
}

boost::optional<geometry_msgs::msg::PoseStamped> HdMapUtils::toMapPose(
  std::int64_t lanelet_id, double s,
  double offset,
  geometry_msgs::msg::Quaternion quat)
{
  geometry_msgs::msg::PoseStamped ret;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto straight_lanelet_ids = getNextLaneletIds(lanelet.id(), "straight");
  boost::optional<lanelet::Lanelet> next_lanelet = boost::none;
  if (straight_lanelet_ids.size() == 0) {
    const auto following_lanelet_ids = getNextLaneletIds(lanelet.id());
    if (following_lanelet_ids.size() != 0) {
      next_lanelet = lanelet_map_ptr_->laneletLayer.get(following_lanelet_ids[0]);
    }
  } else {
    next_lanelet = lanelet_map_ptr_->laneletLayer.get(straight_lanelet_ids[0]);
  }

  const auto centerline = lanelet.centerline();
  std::vector<double> base_x;
  std::vector<double> base_y;
  std::vector<double> base_z;
  for (const auto & pt : centerline) {
    base_x.push_back(pt.x());
    base_y.push_back(pt.y());
    base_z.push_back(pt.z());
  }
  if (next_lanelet) {
    const auto next_centerline = next_lanelet->centerline();
    int count = 0;
    for (const auto & pt : next_centerline) {
      if (count != 0) {
        base_x.push_back(pt.x());
        base_y.push_back(pt.y());
        base_z.push_back(pt.z());
      }
      count++;
    }
  }
  spline_interpolation::SplineInterpolator spline;
  std::vector<double> base_s = calcEuclidDist(base_x, base_y, base_z);
  std::vector<double> resampled_x;
  std::vector<double> resampled_y;
  std::vector<double> resampled_z;
  std::vector<double> resampled_s;
  double diff = 0.01;
  resampled_s.push_back(s);
  resampled_s.push_back(s + diff);
  if (
    !spline.interpolate(
      base_s, base_x, resampled_s, resampled_x, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_y, resampled_s, resampled_y, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_z, resampled_s, resampled_z, spline_interpolation::Method::SOR))
  {
    return boost::none;
  }
  geometry_msgs::msg::Vector3 tangent_vec;
  tangent_vec.x = (resampled_x[1] - resampled_x[0]) / diff;
  tangent_vec.y = (resampled_y[1] - resampled_y[0]) / diff;
  tangent_vec.z = (resampled_z[1] - resampled_z[0]) / diff;
  geometry_msgs::msg::Vector3 rpy;
  rpy.x = 0.0;
  rpy.y = 0.0;
  rpy.z = std::atan2(tangent_vec.y, tangent_vec.x);
  ret.pose.position.x = resampled_x[0] - std::sin(rpy.z) * offset;
  ret.pose.position.y = resampled_y[0] - std::cos(rpy.z) * offset;
  ret.pose.position.z = (resampled_z[1] + resampled_z[0]) * 0.5;
  ret.pose.orientation = quaternion_operation::convertEulerAngleToQuaternion(rpy);
  ret.pose.orientation = ret.pose.orientation * quat;
  ret.header.frame_id = "map";
  return ret;
}

boost::optional<geometry_msgs::msg::PoseStamped> HdMapUtils::toMapPose(
  std::int64_t lanelet_id, double s,
  double offset,
  geometry_msgs::msg::Vector3 rpy)
{
  return toMapPose(lanelet_id, s, offset, quaternion_operation::convertEulerAngleToQuaternion(rpy));
}

boost::optional<geometry_msgs::msg::PoseStamped> HdMapUtils::toMapPose(
  std::int64_t lanelet_id, double s,
  double offset)
{
  geometry_msgs::msg::Vector3 rpy;
  rpy.x = 0;
  rpy.y = 0;
  rpy.z = 0;
  return toMapPose(lanelet_id, s, offset, rpy);
}

boost::optional<geometry_msgs::msg::Vector3> HdMapUtils::getTangentVector(
  std::int64_t lanelet_id,
  double s)
{
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  std::vector<double> base_x;
  std::vector<double> base_y;
  std::vector<double> base_z;
  const auto centerline = lanelet.centerline();
  for (const auto & pt : centerline) {
    base_x.push_back(pt.x());
    base_y.push_back(pt.y());
    base_z.push_back(pt.z());
  }
  spline_interpolation::SplineInterpolator spline;
  std::vector<double> base_s = calcEuclidDist(base_x, base_y, base_z);
  std::vector<double> resampled_x;
  std::vector<double> resampled_y;
  std::vector<double> resampled_z;
  std::vector<double> resampled_s;
  double diff = 0.01;
  resampled_s.push_back(s);
  resampled_s.push_back(s + diff);
  if (
    !spline.interpolate(
      base_s, base_x, resampled_s, resampled_x, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_y, resampled_s, resampled_y, spline_interpolation::Method::SOR) ||
    !spline.interpolate(
      base_s, base_z, resampled_s, resampled_z, spline_interpolation::Method::SOR))
  {
    return boost::none;
  }
  geometry_msgs::msg::Vector3 tangent_vec;
  tangent_vec.x = (resampled_x[1] - resampled_x[0]) / diff;
  tangent_vec.y = (resampled_y[1] - resampled_y[0]) / diff;
  tangent_vec.z = (resampled_z[1] - resampled_z[0]) / diff;
  double vec_size = std::sqrt(
    tangent_vec.x * tangent_vec.x + tangent_vec.y * tangent_vec.y + tangent_vec.z *
    tangent_vec.z);
  tangent_vec.x = tangent_vec.x / vec_size;
  tangent_vec.y = tangent_vec.y / vec_size;
  tangent_vec.z = tangent_vec.z / vec_size;
  return tangent_vec;
}

bool HdMapUtils::canChangeLane(std::int64_t from_lanelet_id, std::int64_t to_lanelet_id)
{
  const auto from_lanelet = lanelet_map_ptr_->laneletLayer.get(from_lanelet_id);
  const auto to_lanelet = lanelet_map_ptr_->laneletLayer.get(to_lanelet_id);
  return traffic_rules_vehicle_ptr_->canChangeLane(from_lanelet, to_lanelet);
}

boost::optional<double> HdMapUtils::getLongitudinalDistance(
  std::int64_t from_lanelet_id, double from_s,
  std::int64_t to_lanelet_id, double to_s)
{
  if (from_lanelet_id == to_lanelet_id) {
    if (from_s > to_s) {
      return boost::none;
    } else {
      return to_s - from_s;
    }
  }
  // double dist_from = getLaneletLength(from_lanelet_id) - from_s;
  const auto lanelet = lanelet_map_ptr_->laneletLayer.get(from_lanelet_id);
  const auto to_lanelet = lanelet_map_ptr_->laneletLayer.get(to_lanelet_id);
  lanelet::Optional<lanelet::routing::Route> route = vehicle_routing_graph_ptr_->getRoute(lanelet,
      to_lanelet, 0,
      true);
  if (!route) {
    // std::cout << "failed to get route" << std::endl;
    return boost::none;
  }
  lanelet::routing::LaneletPath shortest_path = route->shortestPath();
  double dist = 0.0;
  if (shortest_path.empty()) {
    // std::cout << "failed to find shortest path" << std::endl;
    return boost::none;
  }
  for (auto lane_itr = shortest_path.begin(); lane_itr != shortest_path.end(); lane_itr++) {
    if (lane_itr->id() == from_lanelet_id) {
      dist = dist + getLaneletLength(from_lanelet_id) - from_s;
    } else if (lane_itr->id() == to_lanelet_id) {
      dist = dist + to_s;
    } else {
      dist = dist + getLaneletLength(lane_itr->id());
    }
  }
  return dist;
}

const autoware_auto_msgs::msg::HADMapBin HdMapUtils::toMapBin()
{
  std::stringstream ss;
  boost::archive::binary_oarchive oa(ss);
  oa << *lanelet_map_ptr_;
  auto id_counter = lanelet::utils::getId();
  oa << id_counter;
  std::string tmp_str = ss.str();
  autoware_auto_msgs::msg::HADMapBin msg;
  msg.data.clear();
  msg.data.resize(tmp_str.size());
  msg.data.assign(tmp_str.begin(), tmp_str.end());
  msg.header.frame_id = "map";
  return msg;
}

void HdMapUtils::insertMarkerArray(
  visualization_msgs::msg::MarkerArray & a1,
  const visualization_msgs::msg::MarkerArray & a2) const
{
  a1.markers.insert(a1.markers.end(), a2.markers.begin(), a2.markers.end());
}

const visualization_msgs::msg::MarkerArray HdMapUtils::generateMarker() const
{
  visualization_msgs::msg::MarkerArray markers;
  lanelet::ConstLanelets all_lanelets = lanelet::utils::query::laneletLayer(lanelet_map_ptr_);
  lanelet::ConstLanelets road_lanelets = lanelet::utils::query::roadLanelets(all_lanelets);
  lanelet::ConstLanelets crosswalk_lanelets =
    lanelet::utils::query::crosswalkLanelets(all_lanelets);
  lanelet::ConstLanelets walkway_lanelets = lanelet::utils::query::walkwayLanelets(all_lanelets);
  std::vector<lanelet::ConstLineString3d> stop_lines =
    lanelet::utils::query::stopLinesLanelets(road_lanelets);
  std::vector<lanelet::TrafficLightConstPtr> tl_reg_elems =
    lanelet::utils::query::trafficLights(all_lanelets);
  std::vector<lanelet::AutowareTrafficLightConstPtr> aw_tl_reg_elems =
    lanelet::utils::query::autowareTrafficLights(all_lanelets);
  std::vector<lanelet::DetectionAreaConstPtr> da_reg_elems =
    lanelet::utils::query::detectionAreas(all_lanelets);
  lanelet::ConstLineStrings3d parking_spaces =
    lanelet::utils::query::getAllParkingSpaces(lanelet_map_ptr_);
  lanelet::ConstPolygons3d parking_lots =
    lanelet::utils::query::getAllParkingLots(lanelet_map_ptr_);

  auto cl_ll_borders = color_utils::fromRgba(1.0, 1.0, 1.0, 0.999);
  auto cl_road = color_utils::fromRgba(0.2, 0.7, 0.7, 0.3);
  auto cl_cross = color_utils::fromRgba(0.2, 0.7, 0.2, 0.3);
  auto cl_stoplines = color_utils::fromRgba(1.0, 0.0, 0.0, 0.5);
  auto cl_trafficlights = color_utils::fromRgba(0.7, 0.7, 0.7, 0.8);
  auto cl_detection_areas = color_utils::fromRgba(0.7, 0.7, 0.7, 0.3);
  auto cl_parking_lots = color_utils::fromRgba(0.7, 0.7, 0.0, 0.3);
  auto cl_parking_spaces = color_utils::fromRgba(1.0, 0.647, 0.0, 0.6);
  auto cl_lanelet_id = color_utils::fromRgba(0.8, 0.2, 0.2, 0.999);

  insertMarkerArray(
    markers, lanelet::visualization::laneletsBoundaryAsMarkerArray(
      road_lanelets, cl_ll_borders, true));
  insertMarkerArray(
    markers,
    lanelet::visualization::laneletsAsTriangleMarkerArray("road_lanelets",
    road_lanelets, cl_road));
  insertMarkerArray(
    markers, lanelet::visualization::laneletsAsTriangleMarkerArray(
      "crosswalk_lanelets", crosswalk_lanelets, cl_cross));
  insertMarkerArray(
    markers, lanelet::visualization::laneletsAsTriangleMarkerArray(
      "walkway_lanelets", walkway_lanelets, cl_cross));
  insertMarkerArray(
    markers, lanelet::visualization::laneletDirectionAsMarkerArray(road_lanelets));
  insertMarkerArray(
    markers,
    lanelet::visualization::lineStringsAsMarkerArray(stop_lines, "stop_lines", cl_stoplines));
  insertMarkerArray(
    markers,
    lanelet::visualization::autowareTrafficLightsAsMarkerArray(aw_tl_reg_elems, cl_trafficlights));
  insertMarkerArray(
    markers,
    lanelet::visualization::detectionAreasAsMarkerArray(da_reg_elems, cl_detection_areas));
  insertMarkerArray(
    markers,
    lanelet::visualization::parkingLotsAsMarkerArray(parking_lots, cl_parking_lots));
  insertMarkerArray(
    markers,
    lanelet::visualization::parkingSpacesAsMarkerArray(parking_spaces, cl_parking_spaces));
  insertMarkerArray(
    markers,
    lanelet::visualization::generateLaneletIdMarker(road_lanelets, cl_lanelet_id));
  insertMarkerArray(
    markers,
    lanelet::visualization::generateLaneletIdMarker(crosswalk_lanelets, cl_lanelet_id));
  return markers;
}

void HdMapUtils::overwriteLaneletsCenterline()
{
  for (auto & lanelet_obj : lanelet_map_ptr_->laneletLayer) {
    if (!lanelet_obj.hasCustomCenterline()) {
      const auto fine_center_line = generateFineCenterline(lanelet_obj, 2.0);
      lanelet_obj.setCenterline(fine_center_line);
    }
  }
}

std::pair<size_t, size_t> HdMapUtils::findNearestIndexPair(
  const std::vector<double> & accumulated_lengths, const double target_length)
{
  // List size
  const auto N = accumulated_lengths.size();
  // Front
  if (target_length < accumulated_lengths.at(1)) {
    return std::make_pair(0, 1);
  }
  // Back
  if (target_length > accumulated_lengths.at(N - 2)) {
    return std::make_pair(N - 2, N - 1);
  }

  // Middle
  for (size_t i = 1; i < N; ++i) {
    if (
      accumulated_lengths.at(i - 1) <= target_length &&
      target_length <= accumulated_lengths.at(i))
    {
      return std::make_pair(i - 1, i);
    }
  }

  // Throw an exception because this never happens
  throw HdMapError("findNearestIndexPair(): No nearest point found.");
}

const std::unordered_map<std::int64_t,
  std::vector<std::int64_t>> HdMapUtils::getRightOfWayLaneletIds(
  std::vector<std::int64_t> lanelet_ids) const
{
  std::unordered_map<std::int64_t, std::vector<std::int64_t>> ret;
  for (const auto & lanelet_id : lanelet_ids) {
    ret.emplace(lanelet_id, getRightOfWayLaneletIds(lanelet_id));
  }
  return ret;
}

const std::vector<std::int64_t> HdMapUtils::getRightOfWayLaneletIds(std::int64_t lanelet_id) const
{
  std::vector<std::int64_t> ret;
  const auto & assigned_lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
  const auto right_of_ways = assigned_lanelet.regulatoryElementsAs<lanelet::RightOfWay>();
  for (const auto & right_of_way : right_of_ways) {
    const auto right_of_Way_lanelets = right_of_way->rightOfWayLanelets();
    for (const auto & ll : right_of_Way_lanelets) {
      ret.emplace_back(ll.id());
    }
  }
  return ret;
}

std::vector<std::shared_ptr<const lanelet::TrafficSign>>
HdMapUtils::getTrafficSignRegElementsOnPath(std::vector<std::int64_t> lanelet_ids)
{
  std::vector<std::shared_ptr<const lanelet::TrafficSign>> ret;
  for (const auto & lanelet_id : lanelet_ids) {
    const auto lanelet = lanelet_map_ptr_->laneletLayer.get(lanelet_id);
    const auto traffic_signs = lanelet.regulatoryElementsAs<const lanelet::TrafficSign>();
    for (const auto traffic_sign : traffic_signs) {
      ret.push_back(traffic_sign);
    }
  }
  return ret;
}

std::vector<lanelet::ConstLineString3d> HdMapUtils::getStopLinesOnPath(
  std::vector<std::int64_t> lanelet_ids)
{
  std::vector<lanelet::ConstLineString3d> ret;
  const auto traffic_signs = getTrafficSignRegElementsOnPath(lanelet_ids);
  for (const auto & traffic_sign : traffic_signs) {
    if (traffic_sign->type() != "stop_sign") {
      continue;
    }
    for (const auto & stop_line : traffic_sign->refLines()) {
      ret.emplace_back(stop_line);
    }
  }
  return ret;
}

boost::optional<double> HdMapUtils::getDistanceToStopLine(
  std::vector<std::int64_t> following_lanelets,
  std::int64_t lanelet_id, double s)
{
  std::vector<std::int64_t> lanelet_ids;
  std::vector<double> s_values;
  bool stop_lines_found = false;
  std::int64_t stop_lanelet_id;
  std::vector<lanelet::ConstLineString3d> stop_lines;
  for (const auto & following_lanelet_id : following_lanelets) {
    stop_lines = getStopLinesOnPath({following_lanelet_id});
    if (stop_lines.size() == 0) {
      continue;
    }
    stop_lines_found = true;
    stop_lanelet_id = following_lanelet_id;
    break;
  }
  if (stop_lines_found) {
    namespace bg = boost::geometry;
    typedef bg::model::d2::point_xy<double> bg_point;
    const auto center_lines = getCenterPoints(stop_lanelet_id);
    if (center_lines.size() <= 1) {
      return boost::none;
    }
    bool intersection_found = false;
    double intersection_s = 0;
    for (size_t point_index = 0; point_index < (center_lines.size() - 1); point_index++) {
      bg::model::linestring<bg_point> center_line_bg =
        boost::assign::list_of<bg_point>(center_lines[point_index].x,
          center_lines[point_index].y)(center_lines[point_index + 1].x,
          center_lines[point_index + 1].y);
      std::set<double> s_values_in_segment;
      for (const auto & stop_line : stop_lines) {
        for (size_t i = 0; i < (stop_line.size() - 1); i++) {
          const lanelet::ConstPoint3d ps_0 = stop_line[i];
          const lanelet::ConstPoint3d ps_1 = stop_line[i + 1];
          bg::model::linestring<bg_point> stop_line_bg =
            boost::assign::list_of<bg_point>(ps_0.x(), ps_0.y())(ps_1.x(), ps_1.y());
          std::vector<bg_point> result;
          bg::intersection(center_line_bg, stop_line_bg, result);
          if (result.size() != 0) {
            s_values_in_segment.insert(std::hypot(result[0].x() - center_lines[point_index].x,
              result[0].y() - center_lines[point_index].y));
          }
        }
      }
      if (s_values_in_segment.size() == 0) {
        intersection_s = intersection_s +
          std::hypot(center_lines[point_index + 1].x - center_lines[point_index].x,
            center_lines[point_index + 1].y - center_lines[point_index].y);
      } else {
        intersection_s = intersection_s + *s_values_in_segment.begin();
        intersection_found = true;
        break;
      }
    }
    if (intersection_found) {
      return getLongitudinalDistance(lanelet_id, s, stop_lanelet_id, intersection_s);
    }
  }
  return boost::none;
}

std::vector<double> HdMapUtils::calculateSegmentDistances(
  const lanelet::ConstLineString3d & line_string)
{
  std::vector<double> segment_distances;
  segment_distances.reserve(line_string.size() - 1);
  for (size_t i = 1; i < line_string.size(); ++i) {
    const auto distance = lanelet::geometry::distance(line_string[i], line_string[i - 1]);
    segment_distances.push_back(distance);
  }
  return segment_distances;
}

std::vector<double> HdMapUtils::calculateAccumulatedLengths(
  const lanelet::ConstLineString3d & line_string)
{
  const auto segment_distances = calculateSegmentDistances(line_string);

  std::vector<double> accumulated_lengths{0};
  accumulated_lengths.reserve(segment_distances.size() + 1);
  std::partial_sum(
    std::begin(segment_distances), std::end(segment_distances),
    std::back_inserter(accumulated_lengths));
  return accumulated_lengths;
}

std::vector<lanelet::BasicPoint3d> HdMapUtils::resamplePoints(
  const lanelet::ConstLineString3d & line_string, const int32_t num_segments)
{
  // Calculate length
  const auto line_length = lanelet::geometry::length(line_string);

  // Calculate accumulated lengths
  const auto accumulated_lengths = calculateAccumulatedLengths(line_string);

  // Create each segment
  std::vector<lanelet::BasicPoint3d> resampled_points;
  for (auto i = 0; i <= num_segments; ++i) {
    // Find two nearest points
    const double target_length = (static_cast<double>(i) / num_segments) *
      static_cast<double>(line_length);
    const auto index_pair = findNearestIndexPair(accumulated_lengths, target_length);

    // Apply linear interpolation
    const lanelet::BasicPoint3d back_point = line_string[index_pair.first];
    const lanelet::BasicPoint3d front_point = line_string[index_pair.second];
    const auto direction_vector = (front_point - back_point);

    const auto back_length = accumulated_lengths.at(index_pair.first);
    const auto front_length = accumulated_lengths.at(index_pair.second);
    const auto segment_length = front_length - back_length;
    const auto target_point =
      back_point + (direction_vector * (target_length - back_length) / segment_length);

    // Add to list
    resampled_points.push_back(target_point);
  }
  return resampled_points;
}

lanelet::LineString3d HdMapUtils::generateFineCenterline(
  const lanelet::ConstLanelet & lanelet_obj, const double resolution)
{
  // Get length of longer border
  const double left_length =
    static_cast<double>(lanelet::geometry::length(lanelet_obj.leftBound()));
  const double right_length =
    static_cast<double>(lanelet::geometry::length(lanelet_obj.rightBound()));
  const double longer_distance = (left_length > right_length) ? left_length : right_length;
  const int32_t num_segments =
    std::max(static_cast<int32_t>(ceil(longer_distance / resolution)), 1);

  // Resample points
  const auto left_points = resamplePoints(lanelet_obj.leftBound(), num_segments);
  const auto right_points = resamplePoints(lanelet_obj.rightBound(), num_segments);

  // Create centerline
  lanelet::LineString3d centerline(lanelet::utils::getId());
  for (size_t i = 0; i < static_cast<size_t>(num_segments + 1); i++) {
    // Add ID for the average point of left and right
    const auto center_basic_point = (right_points.at(i) + left_points.at(i)) / 2.0;
    const lanelet::Point3d center_point(
      lanelet::utils::getId(), center_basic_point.x(), center_basic_point.y(),
      center_basic_point.z());
    centerline.push_back(center_point);
  }
  return centerline;
}

std::vector<double> HdMapUtils::calcEuclidDist(
  const std::vector<double> & x,
  const std::vector<double> & y,
  const std::vector<double> & z)
{
  std::vector<double> dist_v;
  dist_v.push_back(0.0);
  for (size_t i = 0; i < x.size() - 1; ++i) {
    const double dx = x.at(i + 1) - x.at(i);
    const double dy = y.at(i + 1) - y.at(i);
    const double dz = z.at(i + 1) - z.at(i);
    const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
    dist_v.push_back(dist_v.at(i) + d);
  }
  return dist_v;
}
}  // namespace hdmap_utils