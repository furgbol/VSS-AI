// ® Copyright FURGBot 2019


#include "labels/labels.h"
#include "operation/operation.h"
#include "io/serial_sender.h"

#include "json.hpp"

#include <fstream>
#include <iostream>


namespace vss_furgbol {
namespace io {

SerialSender::SerialSender(int execution_mode, int team_color, bool *running, bool *changed, bool *gk_is_running, bool *cb_is_running, bool *st_is_running, std::queue<std::vector<uint8_t>> *gk_sending_queue, std::queue<std::vector<uint8_t>> *cb_sending_queue, std::queue<std::vector<uint8_t>> *st_sending_queue) :
    gk_sending_queue_(gk_sending_queue), cb_sending_queue_(cb_sending_queue), st_sending_queue_(st_sending_queue),
    mode_(execution_mode), io_service_(), port_(io_service_), buffer_(buf_.data()), running_(running),
    changed_(changed), which_queue_(GK), gk_is_running_(gk_is_running), cb_is_running_(cb_is_running),
    st_is_running_(st_is_running) {}

SerialSender::~SerialSender() {}

void SerialSender::init() {
    configure();

    if (mode_ == REAL) {
        try {
            port_.open(port_name_);
            port_.set_option(boost::asio::serial_port_base::baud_rate(115200));
            port_.set_option(boost::asio::serial_port_base::character_size(8));
        } catch (boost::system::system_error error) {
            std::cout << "[SERIAL COMMUNICATOR ERROR]: " << error.what() << std::endl;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                *running_ = false;
            }
        }
        if (*running_) printConfigurations();
    } else if (mode_ == SIMULATION) {
        try {
            command_sender_ = new vss::CommandSender();
            switch (team_color_) {
                case BLUE:
                    command_sender_->createSocket(vss::TeamType::Blue);
                    break;
                case YELLOW:
                    command_sender_->createSocket(vss::TeamType::Yellow);
                    break;
            }
        } catch (zmq::error_t& error) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                *running_ = false;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *changed_ = true;
    }

    exec();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        *changed_ = true;
    }
}

void SerialSender::configure() {
    std::cout << std::endl << "[STATUS]: Configuring serial..." << std::endl;
    std::ifstream _ifstream("config/serial.json");
    nlohmann::json json_file;
    _ifstream >> json_file;

    port_name_ = json_file["port name"];
    frequency_ = json_file["sending frequency"];
    period_ = 1/(float)frequency_;
}

void SerialSender::printConfigurations() {
    std::cout << "[STATUS]: Serial configuration done!" << std::endl;

    std::cout << "-> Configurations:" << std::endl;
    std::cout << "Serial port: " << port_name_ << std::endl;
    std::cout << "Serial sending frequency: " << frequency_ << "hz" << std::endl;
    std::cout << "Time between serial messages: " << period_ << "s" << std::endl;
    std::cout << std::endl;
}


void SerialSender::exec() {
    std::chrono::system_clock::time_point compair_time = std::chrono::high_resolution_clock::now();
    bool previous_status = false;

    while (true) {
        while (*running_) {
            if (previous_status == false) {
                previous_status = true;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    *changed_ = true;
                }
            }

            if ((std::chrono::high_resolution_clock::now() - compair_time) >= sending_frequency_) {
                switch (which_queue_) {
                    case GK:
                        if (*gk_is_running_) {
                            if (!gk_sending_queue_->empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    send(gk_sending_queue_->front());
                                }
                            }
                            break;
                        } else if (mode_ == SIMULATION) {
                            command_.commands.push_back(vss::WheelsCommand(0, 0));
                        }
                    case CB:
                        if (*cb_is_running_) {
                            if (!cb_sending_queue_->empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    send(cb_sending_queue_->front());
                                }
                            }
                            break;
                        } else if (mode_ == SIMULATION) {
                            command_.commands.push_back(vss::WheelsCommand(0, 0));
                        }
                    case ST:
                        if (*st_is_running_) {
                            if (!st_sending_queue_->empty()) {
                                {
                                    std::lock_guard<std::mutex> lock(mutex_);
                                    send(st_sending_queue_->front());
                                }
                            }
                            break;
                        } else if (mode_ == SIMULATION) {
                            command_.commands.push_back(vss::WheelsCommand(0, 0));
                        }
                }
                which_queue_++;
                if (which_queue_ > ST) {
                    which_queue_ = GK;
                    command_sender_->sendCommand(command_);
                    std::cout << "Sended: {" << std::endl;
                    std::cout << "\tRobot 1:" << std::endl;
                    std::cout << "\t\t-> Velocity Right: " << command_.commands[GK].rightVel << std::endl;
                    std::cout << "\t\t-> Velocity Left: " << command_.commands[GK].leftVel << std::endl;
                    // std::cout << "\tRobot 2:" << std::endl;
                    // std::cout << "\t\t-> Velocity Right: " << command_.commands[CB].rightVel << std::endl;
                    // std::cout << "\t\t-> Velocity Left: " << command_.commands[CB].leftVel << std::endl;
                    // std::cout << "\tRobot 3:" << std::endl;
                    // std::cout << "\t\t-> Velocity Right: " << command_.commands[ST].rightVel << std::endl;
                    // std::cout << "\t\t-> Velocity Left: " << command_.commands[ST].leftVel << std::endl;
                    // std::cout << "}" << std::endl;
                    command_.commands.clear();
                }
                compair_time = std::chrono::high_resolution_clock::now();
            }
        }

        if (!*running_) {
            end();
            
            if (previous_status == true) {
                previous_status = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    *changed_ = true;
                }
            }

            break;
        }
    }

    /*while (running_) {
        if (!gk_sending_queue_->empty()) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                buffer = gk_sending_queue_->front();
            }
 
            std::cout << std::endl << "Buffer:" << std::endl;
            std::cout << "\tRobot ID: " << (int)buffer[operation::ROBOT_ID] << std::endl;
            std::cout << "\tLinear Velocity: " << (int)buffer[operation::LINEAR_VELOCITY] << std::endl;
            std::cout << "\tAngular Velocity: " << (int)buffer[operation::ANGULAR_VELOCITY] << std::endl;
            std::cout << "\tLinear Direction: " << (int)buffer[operation::LINEAR_DIRECTION] << std::endl;
            std::cout << "\tAngular Direction: " << (int)buffer[operation::ANGULAR_DIRECTION] << std::endl;
            send(buffer);
            compair_time = std::chrono::high_resolution_clock::now();
        }
    }*/
}

void SerialSender::end() {
    std::cout << "[STATUS]: Closing serial..." << std::endl;
    try {
        port_.close();
    } catch (boost::system::system_error error) {}
}

void SerialSender::send(std::vector<unsigned char> buffer) { 
    if (buffer[operation::ROBOT_ID] >= 128) {
        switch (mode_) {
            case REAL:
                port_.write_some(boost::asio::buffer(buffer, buffer.size()));
                break;
            case SIMULATION:
                linear_velocity_ = (float)buffer[operation::LINEAR_VELOCITY];
                angular_velocity_ = (float)buffer[operation::ANGULAR_VELOCITY];
                linear_direction_ = buffer[operation::LINEAR_DIRECTION];
                angular_direction_ = buffer[operation::ANGULAR_DIRECTION];
                calculateVelocity();
                command_.commands.push_back(vss::WheelsCommand(velocity_right_, velocity_left_));
        }
    }
}

void SerialSender::calculateVelocity() {
    linear_velocity_ = ((20 * linear_velocity_) / 127.0) * (linear_direction_ - 2);
    angular_velocity_ = ((20 * angular_velocity_) / 127.0) * (angular_direction_ - 2);
    velocity_right_ = ((linear_velocity_ / 0.03) + ((angular_velocity_ * 0.04) / 0.03));
    velocity_left_ = ((linear_velocity_ / 0.03) - ((angular_velocity_ * 0.04) / 0.03));
}

} // namespace io
} // namespace vss_furgbol