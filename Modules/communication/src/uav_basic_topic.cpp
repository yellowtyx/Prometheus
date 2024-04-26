#include "uav_basic_topic.hpp"

UAVBasic::UAVBasic()
{

}

UAVBasic::UAVBasic(ros::NodeHandle &nh,int id,Communication *communication)
{
    nh.param<std::string>("multicast_udp_ip", multicast_udp_ip, "224.0.0.88");
    nh.param<std::string>("ground_station_ip", ground_station_ip, "127.0.0.1");
    nh.param<int>("uav_basic_hz", send_hz, 0);

    this->robot_id = id;
    this->offset_pose_.x = 0;
    this->offset_pose_.y = 0;
    this->communication_ = communication;

    //【订阅】uav状态信息
    this->uav_state_sub_ = nh.subscribe("/uav" + std::to_string(this->robot_id) + "/prometheus/state", 10, &UAVBasic::stateCb, this);
    //【订阅】uav反馈信息
    this->text_info_sub_ = nh.subscribe("/uav" + std::to_string(id) + "/prometheus/text_info", 10, &UAVBasic::textInfoCb, this);
    //【订阅】uav控制状态信息
    this->uav_control_state_sub_ = nh.subscribe("/uav" + std::to_string(id) + "/prometheus/control_state", 10, &UAVBasic::controlStateCb, this);
    //【发布】底层控制指令(-> uav_control.cpp)
    this->uav_cmd_pub_ = nh.advertise<prometheus_msgs::UAVCommand>("/uav" + std::to_string(id) + "/prometheus/command", 1);
    //【发布】mavros接口调用指令(-> uav_control.cpp)
    this->uav_setup_pub_ = nh.advertise<prometheus_msgs::UAVSetup>("/uav" + std::to_string(this->robot_id) + "/prometheus/setup", 1);
    //【订阅】uav控制信息
    this->uav_cmd_sub_ = nh.subscribe("/uav" + std::to_string(id) + "/prometheus/command",10,&UAVBasic::uavCmdCb,this);
    //【发布】跟踪目标
    this->uav_target_pub_ = nh.advertise<prometheus_msgs::Control>("/uav" + std::to_string(this->robot_id) + "/spirecv/control", 1);
    //【订阅】PX4中无人机的位置/速度/加速度设定值 坐标系:ENU系
    this->px4_position_target_sub_ = nh.subscribe<mavros_msgs::PositionTarget>("/uav" + std::to_string(this->robot_id) + "/mavros/setpoint_raw/target_local",1,&UAVBasic::px4PosTargetCb, this);
    //【订阅】PX4中无人机的姿态设定值 坐标系:ENU系
    this->px4_attitude_target_sub_ = nh.subscribe<mavros_msgs::AttitudeTarget>("/uav" + std::to_string(this->robot_id) + "/mavros/setpoint_raw/target_attitude",1,&UAVBasic::px4AttTargetCb, this);

    if(send_hz > 0)
    {
        send_timer = nh.createTimer(ros::Duration(1.0/send_hz), &UAVBasic::send, this);
    }
}

UAVBasic::~UAVBasic()
{
    // delete this->communication_;
}

void UAVBasic::stateCb(const prometheus_msgs::UAVState::ConstPtr &msg)
{
    this->uav_state_.secs = msg->header.stamp.sec;
    this->uav_state_.nsecs = msg->header.stamp.nsec;
    this->uav_state_.uav_id = msg->uav_id;
    // this->uav_state_.state = msg->state;
    this->uav_state_.location_source = msg->location_source;
    this->uav_state_.gps_status = msg->gps_status;
    this->uav_state_.mode = msg->mode;
    this->uav_state_.connected = msg->connected;
    this->uav_state_.armed = msg->armed;
    this->uav_state_.odom_valid = msg->odom_valid;
    this->uav_state_.gps_num = msg->gps_num;

    for (int i = 0; i < 3; i++)
    {
        this->uav_state_.velocity[i] = msg->velocity[i];
        this->uav_state_.attitude[i] = msg->attitude[i];
        this->uav_state_.attitude_rate[i] = msg->attitude_rate[i];
    };
    this->uav_state_.position[0] = msg->position[0] + offset_pose_.x;
    this->uav_state_.position[1] = msg->position[1] + offset_pose_.y;
    this->uav_state_.position[2] = msg->position[2];

    this->uav_state_.latitude = msg->latitude;
    this->uav_state_.longitude = msg->longitude;
    this->uav_state_.altitude = msg->altitude;

    this->uav_state_.attitude_q.x = msg->attitude_q.x;
    this->uav_state_.attitude_q.y = msg->attitude_q.y;
    this->uav_state_.attitude_q.z = msg->attitude_q.z;
    this->uav_state_.attitude_q.w = msg->attitude_q.w;
    this->uav_state_.battery_state = msg->battery_state;
    this->uav_state_.battery_percetage = msg->battery_percetage;

    if(send_hz <= 0) this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP, this->uav_state_,this->robot_id), multicast_udp_ip);
    else uav_state_ready = true;
    setTimeStamp(msg->header.stamp.sec);
}

//【回调】uav反馈信息
void UAVBasic::textInfoCb(const prometheus_msgs::TextInfo::ConstPtr &msg)
{
    this->text_info_.sec = msg->header.stamp.sec;
    this->text_info_.MessageType = msg->MessageType;
    this->text_info_.Message = msg->Message;

    //发送到地面站
    this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP, this->text_info_,this->robot_id), ground_station_ip);
}

void UAVBasic::controlStateCb(const prometheus_msgs::UAVControlState::ConstPtr &msg)
{
    this->uav_control_state_.uav_id = msg->uav_id;
    this->uav_control_state_.control_state = msg->control_state;
    this->uav_control_state_.pos_controller = msg->pos_controller;
    this->uav_control_state_.failsafe = msg->failsafe;

    //发送到地面站
    if(send_hz <= 0) this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP, this->uav_control_state_,this->robot_id), ground_station_ip);
    else uav_control_state_ready = true;
}

struct UAVState UAVBasic::getUAVState()
{
    return this->uav_state_;
}

void UAVBasic::uavCmdPub(struct UAVCommand uav_cmd)
{
    prometheus_msgs::UAVCommand uav_cmd_;
    uav_cmd_.header.stamp = ros::Time::now();
    uav_cmd_.Agent_CMD = uav_cmd.Agent_CMD;
    uav_cmd_.Move_mode = uav_cmd.Move_mode;
    for(int i = 0; i < 3; i++)
    {
        uav_cmd_.position_ref[i] = uav_cmd.position_ref[i];
        uav_cmd_.velocity_ref[i] = uav_cmd.velocity_ref[i];
        uav_cmd_.acceleration_ref[i] = uav_cmd.acceleration_ref[i];
        uav_cmd_.att_ref[i] = uav_cmd.att_ref[i];
    }
    uav_cmd_.Control_Level = uav_cmd.Control_Level;
    uav_cmd_.att_ref[3] = uav_cmd.att_ref[3];
    uav_cmd_.yaw_ref = uav_cmd.yaw_ref;
    uav_cmd_.Yaw_Rate_Mode = uav_cmd.Yaw_Rate_Mode;
    uav_cmd_.yaw_rate_ref = uav_cmd.yaw_rate_ref;
    uav_cmd_.Command_ID = uav_cmd.Command_ID;
    uav_cmd_.latitude = uav_cmd.latitude;
    uav_cmd_.longitude = uav_cmd.longitude;
    uav_cmd_.altitude = uav_cmd.altitude;

    this->uav_cmd_pub_.publish(uav_cmd_);
}

void UAVBasic::uavCmdCb(const prometheus_msgs::UAVCommand::ConstPtr &msg)
{
    uav_command_.secs = msg->header.stamp.sec;
    uav_command_.nsecs = msg->header.stamp.nsec;
    uav_command_.Agent_CMD = msg->Agent_CMD;
    uav_command_.Move_mode = msg->Move_mode;
    if(uav_command_.Move_mode == UAVCommand::MoveMode::XYZ_POS_BODY)
        uav_command_.Move_mode = UAVCommand::MoveMode::XYZ_POS;
    // for(int i = 0; i < 3; i++)
    // {
    //     uav_command_.position_ref[i] = msg->position_ref[i];
    //     uav_command_.velocity_ref[i] = msg->velocity_ref[i];
    //     uav_command_.acceleration_ref[i] = msg->acceleration_ref[i];
    //     uav_command_.att_ref[i] = msg->att_ref[i];
    // }
    uav_command_.att_ref[3] = msg->att_ref[3];
    uav_command_.yaw_ref = msg->yaw_ref;
    uav_command_.Yaw_Rate_Mode = msg->Yaw_Rate_Mode;
    uav_command_.yaw_rate_ref = msg->yaw_rate_ref;
    uav_command_.Command_ID = msg->Command_ID;
    uav_command_.latitude = msg->latitude;
    uav_command_.longitude = msg->longitude;
    uav_command_.altitude = msg->altitude;

    if(send_hz <= 0) this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP,uav_command_,this->robot_id),ground_station_ip);
    else uav_command_ready = true;
}

void UAVBasic::px4PosTargetCb(const mavros_msgs::PositionTarget::ConstPtr &msg)
{
    uav_command_.position_ref[0] = msg->position.x;
    uav_command_.position_ref[1] = msg->position.y;
    uav_command_.position_ref[2] = msg->position.z;
    uav_command_.velocity_ref[0] = msg->velocity.x;
    uav_command_.velocity_ref[1] = msg->velocity.y;
    uav_command_.velocity_ref[2] = msg->velocity.z;

    if(send_hz <= 0) this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP,uav_command_,this->robot_id),ground_station_ip);
    else uav_command_ready = true;
}

void UAVBasic::px4AttTargetCb(const mavros_msgs::AttitudeTarget::ConstPtr &msg)
{
    uav_command_.att_ref[0] = msg->orientation.x;
    uav_command_.att_ref[1] = msg->orientation.y;
    uav_command_.att_ref[2] = msg->orientation.z;
    uav_command_.att_ref[3] = msg->orientation.w;
    // 油门？
    // px4_thrust_target = msg->thrust;

    if(send_hz <= 0) this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP,uav_command_,this->robot_id),ground_station_ip);
    else uav_command_ready = true;
}

void UAVBasic::uavSetupPub(struct UAVSetup uav_setup)
{
    prometheus_msgs::UAVSetup uav_setup_;
    uav_setup_.cmd = uav_setup.cmd;
    uav_setup_.arming = uav_setup.arming;
    uav_setup_.control_state = uav_setup.control_state;
    uav_setup_.px4_mode = uav_setup.px4_mode;
    this->uav_setup_pub_.publish(uav_setup_);
}

void UAVBasic::setTimeStamp(uint time)
{
    this->time_stamp_ = time;
}

uint UAVBasic::getTimeStamp()
{
    return this->time_stamp_;
}

void UAVBasic::uavTargetPub(struct WindowPosition window_position)
{
    prometheus_msgs::Control cmd;
    cmd.mouse = window_position.track_id == -1?prometheus_msgs::Control::MOUSE_LEFT:prometheus_msgs::Control::MOUSE_RIGHT;
    cmd.x = window_position.origin_x/(float)window_position.window_position_x;
    cmd.y = window_position.origin_y/(float)window_position.window_position_y;
    this->uav_target_pub_.publish(cmd);
}

void UAVBasic::send(const ros::TimerEvent &time_event)
{
    // std::cout << "uav_basic: " << uav_state_ready << " " << uav_command_ready << " " << uav_control_state_ready << std::endl;
    if(uav_state_ready)
    {
        this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP, this->uav_state_,this->robot_id), multicast_udp_ip);
        this->uav_state_ready = false;
    }
    if(uav_command_ready)
    {
        this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP,this->uav_command_,this->robot_id),ground_station_ip);
        this->uav_command_ready = false;
    }
    if(uav_control_state_ready)
    {
        this->communication_->sendMsgByUdp(this->communication_->encodeMsg(Send_Mode::UDP, this->uav_control_state_,this->robot_id), ground_station_ip);
        this->uav_control_state_ready = false;
    }
}