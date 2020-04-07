//ros头文件
#include <ros/ros.h>
#include <Eigen/Eigen>
#include <iostream>
#include <mission_utils.h>

//topic 头文件
#include <geometry_msgs/Point.h>
#include <prometheus_msgs/ControlCommand.h>
#include <prometheus_msgs/DroneState.h>
#include <prometheus_msgs/DetectionInfo.h>
#include <prometheus_msgs/MultiDetectionInfo.h>
#include <nav_msgs/Odometry.h>
#include <geometry_msgs/PoseStamped.h>
#include <geometry_msgs/TwistStamped.h>
#include <mavros_msgs/ActuatorControl.h>
#include <sensor_msgs/Imu.h>
#include <prometheus_msgs/DroneState.h>
#include <prometheus_msgs/AttitudeReference.h>
#include <prometheus_msgs/DroneState.h>
#include <nav_msgs/Path.h>
#include <math.h>
using namespace std;

//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>全 局 变 量<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
prometheus_msgs::DetectionInfo Detection_info;
Eigen::Vector3f pos_body_frame;        //机体系
Eigen::Vector3f pos_body_enu_frame;     //原点位于质心，x轴指向前方，y轴指向左，z轴指向上的坐标系
prometheus_msgs::ControlCommand Command_Now;                               //发送给控制模块 [px4_pos_controller.cpp]的命令
prometheus_msgs::DroneState _DroneState;                                   //无人机状态量
ros::Publisher command_pub;
int State_Machine = 0;
float kpx_track,kpy_track,kpz_track;                                                 //控制参数 - 比例参数
Eigen::Vector3f camera_offset;
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>声 明 函 数<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void tracking();
void crossing();
void return_start_point();
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>回 调 函 数<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
void vision_cb(const prometheus_msgs::DetectionInfo::ConstPtr& msg)
{
    Detection_info = *msg;
    pos_body_frame[0] = Detection_info.position[2] + camera_offset[0];
    pos_body_frame[1] = - Detection_info.position[0] + camera_offset[1];
    pos_body_frame[2] = - Detection_info.position[1] + camera_offset[2];

    Eigen::Matrix3f R_Body_to_ENU;

    R_Body_to_ENU = get_rotation_matrix(_DroneState.attitude[0], _DroneState.attitude[1], _DroneState.attitude[2]);

    pos_body_enu_frame = R_Body_to_ENU * pos_body_frame;

}
void drone_state_cb(const prometheus_msgs::DroneState::ConstPtr& msg)
{
    _DroneState = *msg;
}
//>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>主 函 数<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
int main(int argc, char **argv)
{
    ros::init(argc, argv, "circle_crossing");
    ros::NodeHandle nh("~");
    
    //【订阅】图像识别结果，返回的结果为相机坐标系
    //  方向定义： 识别算法发布的目标位置位于相机坐标系（从相机往前看，物体在相机右方x为正，下方y为正，前方z为正）
    //  标志位：   detected 用作标志位 ture代表识别到目标 false代表丢失目标
    ros::Subscriber vision_sub = nh.subscribe<prometheus_msgs::DetectionInfo>("/prometheus/target", 10, vision_cb);

    //【订阅】无人机当前状态
    ros::Subscriber drone_state_sub = nh.subscribe<prometheus_msgs::DroneState>("/prometheus/drone_state", 10, drone_state_cb);
    
    // 【发布】发送给控制模块 [px4_pos_controller.cpp]的命令
    command_pub = nh.advertise<prometheus_msgs::ControlCommand>("/prometheus/control_command", 10);

    nh.param<float>("kpx_track", kpx_track, 0.1);
    nh.param<float>("kpy_track", kpy_track, 0.1);
    nh.param<float>("kpz_track", kpz_track, 0.1);

    nh.param<float>("camera_offset_x", camera_offset[0], 0.0);
    nh.param<float>("camera_offset_y", camera_offset[1], 0.0);
    nh.param<float>("camera_offset_z", camera_offset[2], 0.0);

    //固定的浮点显示
    cout.setf(ios::fixed);
    //setprecision(n) 设显示小数精度为n位
    cout<<setprecision(2);
    //左对齐
    cout.setf(ios::left);
    // 强制显示小数点
    cout.setf(ios::showpoint);
    // 强制显示符号
    cout.setf(ios::showpos);

    // Waiting for input
    int start_flag = 0;
    while(start_flag == 0)
    {
        cout << ">>>>>>>>>>>>>>>>>>>>>>>>>>>>>Circle Crossing Mission<<<<<<<<<<<<<<<<<<<<<<<<<<< "<< endl;
        cout << "Please enter 1 to takeoff the drone..."<<endl;
        cin >> start_flag;
    }

    // 起飞
    Command_Now.Command_ID = 1;
    while( _DroneState.position[2] < 0.3)
    {
        Command_Now.header.stamp = ros::Time::now();
        Command_Now.Mode  = prometheus_msgs::ControlCommand::Idle;
        Command_Now.Command_ID = Command_Now.Command_ID + 1;
        Command_Now.Reference_State.yaw_ref = 999;
        command_pub.publish(Command_Now);   
        cout << "Switch to OFFBOARD and arm ..."<<endl;
        ros::Duration(3.0).sleep();
        
        Command_Now.header.stamp = ros::Time::now();
        Command_Now.Mode                                = prometheus_msgs::ControlCommand::Move;
        Command_Now.Command_ID                          = Command_Now.Command_ID + 1;
        Command_Now.Reference_State.Move_mode           = prometheus_msgs::PositionReference::XYZ_POS;
        Command_Now.Reference_State.Move_frame          = prometheus_msgs::PositionReference::ENU_FRAME;
        Command_Now.Reference_State.position_ref[0]     = 0.0;
        Command_Now.Reference_State.position_ref[1]     = 0.0;
        Command_Now.Reference_State.position_ref[2]     = 1.5;
        Command_Now.Reference_State.yaw_ref             = 0.0;
        command_pub.publish(Command_Now);
        cout << "Takeoff ..."<<endl;
        ros::Duration(3.0).sleep();

        ros::spinOnce();
    }

    while (ros::ok())
    {
        cout << ">>>>>>>>>>>>>>>>>>>>>>>>>>>Circle Crossing Mission<<<<<<<<<<<<<<<<<<<<<<<<<"<<endl;
        
        if(State_Machine == 0)
        {
            cout << "Please enter 1 to start the mission ..."<<endl;
            cin >> start_flag;

            if (start_flag == 1)
            {
                State_Machine = 1;
            }else
            {
                State_Machine = 0;
            }
        }else if(State_Machine == 1)
        {
            tracking();
            cout << "Tracking the circle..." <<  endl;
            cout << "Camera_frame : " << Detection_info.position[0] << " [m] "<< Detection_info.position[1] << " [m] "<< Detection_info.position[2] << " [m] "<<endl;
            cout << "Body_frame   : " << pos_body_frame[0] << " [m] "<< pos_body_frame[1] << " [m] "<< pos_body_frame[2] << " [m] "<<endl;
            cout << "BodyENU_frame: " << pos_body_enu_frame[0] << " [m] "<< pos_body_enu_frame[1] << " [m] "<< pos_body_enu_frame[2] << " [m] "<<endl;
            
        }else if(State_Machine == 2)
        {
            crossing();
            cout << "Crossing the circle..." <<  endl;
        }else if(State_Machine == 3)
        {
            return_start_point();
            cout << "Returning the start point..." <<  endl;
        }

        //回调
        ros::spinOnce();
        ros::Duration(0.05).sleep();
    }

    return 0;

}

void tracking()
{
    Command_Now.Mode                                = prometheus_msgs::ControlCommand::Move;
    Command_Now.Command_ID                          = Command_Now.Command_ID + 1;
    Command_Now.Reference_State.Move_mode           = prometheus_msgs::PositionReference::XYZ_VEL;
    Command_Now.Reference_State.Move_frame          = prometheus_msgs::PositionReference::ENU_FRAME;
    Command_Now.Reference_State.position_ref[0]     = 0;
    Command_Now.Reference_State.position_ref[1]     = 0;
    Command_Now.Reference_State.position_ref[2]     = 0;
    Command_Now.Reference_State.velocity_ref[0]     = kpx_track * pos_body_enu_frame[0];
    Command_Now.Reference_State.velocity_ref[1]     = kpy_track * pos_body_enu_frame[1];
    Command_Now.Reference_State.velocity_ref[2]     = kpz_track * pos_body_enu_frame[2];
    Command_Now.Reference_State.yaw_ref             = 0;

    command_pub.publish(Command_Now);   

    if(abs(pos_body_enu_frame[0]) < 1)
    {
        State_Machine = 2;
    }
}

void crossing()
{
    Command_Now.Mode                                = prometheus_msgs::ControlCommand::Move;
    Command_Now.Command_ID                          = Command_Now.Command_ID + 1;
    Command_Now.Reference_State.Move_mode           = prometheus_msgs::PositionReference::XYZ_POS;
    Command_Now.Reference_State.Move_frame          = prometheus_msgs::PositionReference::BODY_FRAME;
    Command_Now.Reference_State.position_ref[0]     = 2.0;
    Command_Now.Reference_State.position_ref[1]     = 0;
    Command_Now.Reference_State.position_ref[2]     = 0;
    Command_Now.Reference_State.yaw_ref             = 0;

    command_pub.publish(Command_Now);   

    ros::Duration(5.0).sleep();

    State_Machine = 3;
}

void return_start_point()
{
    Command_Now.Mode                                = prometheus_msgs::ControlCommand::Move;
    Command_Now.Command_ID                          = Command_Now.Command_ID + 1;
    Command_Now.Reference_State.Move_mode           = prometheus_msgs::PositionReference::XYZ_POS;
    Command_Now.Reference_State.Move_frame          = prometheus_msgs::PositionReference::BODY_FRAME;
    Command_Now.Reference_State.position_ref[0]     = 0.0;
    Command_Now.Reference_State.position_ref[1]     = 3.0;
    Command_Now.Reference_State.position_ref[2]     = 0.0;
    Command_Now.Reference_State.yaw_ref             = 0.0;

    command_pub.publish(Command_Now);   

    ros::Duration(3.0).sleep();

    Command_Now.Mode                                = prometheus_msgs::ControlCommand::Move;
    Command_Now.Command_ID                          = Command_Now.Command_ID + 1;
    Command_Now.Reference_State.Move_mode           = prometheus_msgs::PositionReference::XYZ_POS;
    Command_Now.Reference_State.Move_frame          = prometheus_msgs::PositionReference::ENU_FRAME;
    Command_Now.Reference_State.position_ref[0]     = 0.0;
    Command_Now.Reference_State.position_ref[1]     = 0.0;
    Command_Now.Reference_State.position_ref[2]     = 1.5;
    Command_Now.Reference_State.yaw_ref             = 0.0;

    command_pub.publish(Command_Now);   

    ros::Duration(5.0).sleep();

    State_Machine = 0;
}