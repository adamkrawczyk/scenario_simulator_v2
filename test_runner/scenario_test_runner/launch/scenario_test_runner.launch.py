#!/usr/bin/env python3
# -*- coding: utf-8 -*-

# Copyright 2020 Tier IV, Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, Shutdown
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import LifecycleNode, Node
from pathlib import Path


def generate_launch_description():
    autoware_launch_file = LaunchConfiguration(
        'autoware-launch-file', default='planning_simulator.launch.xml')
    autoware_launch_package = LaunchConfiguration(
        'autoware-launch-package', default='autoware_launch')
    global_frame_rate = LaunchConfiguration('global-frame-rate', default=30.0)
    global_real_time_factor = LaunchConfiguration('global-real-time-factor', default=1.0)
    global_timeout = LaunchConfiguration('global-timeout', default=180)
    output_directory = LaunchConfiguration('output-directory', default=Path("/tmp"))
    scenario = LaunchConfiguration('scenario', default=Path("/dev/null"))
    workflow = LaunchConfiguration('workflow', default=Path("/dev/null"))

    port = 8080

    return LaunchDescription([
        DeclareLaunchArgument('autoware-launch-file', default_value=autoware_launch_file),

        DeclareLaunchArgument('autoware-launch-package', default_value=autoware_launch_package),

        DeclareLaunchArgument('global-frame-rate', default_value=global_frame_rate),

        DeclareLaunchArgument(
            'global-real-time-factor', default_value=global_real_time_factor,
            description="Specify the ratio of simulation time to real time. If "
            "you set a value greater than 1, the simulation will be faster "
            "than in reality, and if you set a value less than 1, the "
            "simulation will be slower than in reality."),

        DeclareLaunchArgument(
            'global-timeout', default_value=global_timeout,
            description="Specify the simulation time limit. This time limit is "
            "independent of the simulation playback speed determined by the "
            "option real_time_factor. It also has nothing to do with "
            "OpenSCENARIO's SimulationTimeCondition."),

        DeclareLaunchArgument(
            'output-directory', default_value=output_directory,
            description="Specify the output destination directory of the "
            "generated file including the result file."),

        DeclareLaunchArgument(
            'scenario', default_value=scenario,
            description="Specify a scenario file (.yaml or .xosc) you want to "
            "execute. If a workflow file is also specified by the '--workflow' "
            "option at the same time, this option takes precedence (that is, "
            "only one scenario passed to the --scenario option will be executed"
            ")."),

        DeclareLaunchArgument(
            'workflow', default_value=workflow,
            description="Specify a workflow file (.yaml) you want to execute."),

        Node(
            package='scenario_test_runner',
            executable='scenario_test_runner',
            namespace='simulation',
            name='scenario_test_runner',
            output='screen',
            on_exit=Shutdown(),
            arguments=[
                '--global-frame-rate', global_frame_rate,
                '--global-real-time-factor', global_real_time_factor,
                '--global-timeout', global_timeout,
                '--output-directory', output_directory,
                '--scenario', scenario,
                '--workflow', workflow,
                ],),

        Node(
            package='simple_sensor_simulator',
            executable='simple_sensor_simulator_node',
            namespace='simulation',
            name='simple_sensor_simulator',
            output='screen',
            parameters=[
                {'port': port},
                ],),

        LifecycleNode(
            package='openscenario_interpreter',
            executable='openscenario_interpreter_node',
            namespace='simulation',
            name='openscenario_interpreter',
            output='screen',
            parameters=[
                # 'map_path': os.path.join(
                #     get_package_share_directory('kashiwanoha_map'), 'map', 'lanelet2_map.osm'),
                # 'origin_latitude':   34.903555800615614,
                # 'origin_longitude': 139.93339979022568,
                {'autoware_launch_file': autoware_launch_file},
                {'autoware_launch_package': autoware_launch_package},
                {'port': port},
                ],),

        Node(
            package='openscenario_visualization',
            executable='openscenario_visualization_node',
            namespace='simulation',
            name='openscenario_visualizer',
            output='screen',),

        # Node(
        #     package='rviz2',
        #     executable='rviz2',
        #     name='rviz2',
        #     output={
        #         'stderr': 'log',
        #         'stdout': 'log',
        #         },
        #     arguments=[
        #         '-d', str(
        #             Path(get_package_share_directory('scenario_test_runner')) /
        #             'planning_simulator_v2.rviz')
        #         ],
        #     ),
        ])
