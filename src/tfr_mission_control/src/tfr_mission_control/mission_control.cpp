#include "tfr_mission_control/mission_control.h"

#include <pluginlib/class_list_macros.h>

namespace tfr_mission_control {

    /* ========================================================================== */
    /* Constructor/Destructor                                                     */
    /* ========================================================================== */

    /*
     * First thing to get called, NOTE ros::init called by superclass
     * Not needed here
     * */
    MissionControl::MissionControl()
        : rqt_gui_cpp::Plugin(),
        widget(nullptr),
        autonomy{"autonomous_action_server",true},
        teleop{"teleop_action_server",true},
        arm_client{"move_arm", true},
        com{nh.subscribe("com", 5, &MissionControl::updateStatus, this)},
        teleopEnabled{false}
    {
        setObjectName("MissionControl");
    }

    /* NOTE: I think these QObjects have reference counting and are held by
     * smart pointers under the hood. (I never ever see any qt programmers
     * implement destructors or free memory on QObjects in any tutorial, example
     * code or forum). However I do not know for sure, so I do it here to make 
     * myself feel better, and it doesn't seem to complain.
     * */
    MissionControl::~MissionControl()
    {
        delete countdownClock;
        delete motorKill;
        delete widget;
    }

    /* ========================================================================== */
    /* Initialize/Shutdown                                                        */
    /* ========================================================================== */

    /*
     * Actually sets up all of the components on the screen and registers
     * application context. Some of this code is boilerplate generated by that
     * "catkin_creat_rqt" script, and it looked complicated, so I just took it
     * at face value. I'll mark those sections with a //boilerplate annotation.
     * */
    void MissionControl::initPlugin(qt_gui_cpp::PluginContext& context)
    {
        //boilerplate
        widget = new QWidget();
        ui.setupUi(widget);
        if (context.serialNumber() > 1)
        {
            widget->setWindowTitle(widget->windowTitle() +
                    " (" + QString::number(context.serialNumber()) + ")");
        }

        //sets our window active, and makes sure we handle keyboard events
        widget->setFocusPolicy(Qt::StrongFocus);
        widget->installEventFilter(this);
        //boilerplate
        context.addWidget(widget);

        countdownClock = new QTimer(this); //mission clock, runs repeatedly
        motorKill = new QTimer(this); //motor watchdog
        motorKill->setSingleShot(true); //tells it to only run on demand

        /* Sets up all the signal/slot connections.
         *
         * For those unfamilair with qt this is the backbone of event driven
         * programming in qt. Signals get generated when an event of interest
         * happens: button pressed/released, timer expires... You can attach
         * signals to 0 or many slots. Slots are functions that get put into an
         * application level queue and processed when time allows. This allows
         * for thread safety. 
         *
         * Signals can pass data, however slots they attach to must be able to
         * recieve that data, so signatures need to match. Sometimes you don't
         * want to do this, so you can get around it with lambdas (which I end
         * up doing below)
         *
         * Qt might or might not run different ui objects on many different
         * threads, and rqt will be running on a diffent thread. So in general,
         * if you need to have synchronous state passing, do it through the 
         * signal/slot system, and you should be fine. 
         * */
        connect(ui.start_button, &QPushButton::clicked,this, &MissionControl::startMission);
        connect(ui.clock_button, &QPushButton::clicked,this, &MissionControl::startManual);
        connect(ui.autonomy_button, &QPushButton::clicked, this,  &MissionControl::goAutonomousMode);
        connect(ui.teleop_button, &QPushButton::clicked, this,  &MissionControl::goTeleopMode);

        connect(ui.control_enable_button,&QPushButton::clicked, [this] () {toggleControl(true);});
        connect(ui.control_disable_button,&QPushButton::clicked, [this] () {toggleControl(false);});
        connect(ui.motor_enable_button,&QPushButton::clicked, [this] () {toggleMotors(true);});
        connect(ui.motor_disable_button,&QPushButton::clicked, [this] () {toggleMotors(false);});
        connect(countdownClock, &QTimer::timeout, this,  &MissionControl::renderClock);
        connect(this, &MissionControl::emitStatus, ui.status_log, &QPlainTextEdit::appendPlainText);
        connect(ui.status_log, &QPlainTextEdit::textChanged, this,  &MissionControl::renderStatus);

        /* NOTE Remember how I said parameters of signals/slots need to match
         * up. I want to be able to process teleop commands by passing the code
         * into the perform teleop command. I could write a bunch of functors,
         * to make the parameters match up but that could get tedious, luckily
         * qt5 did a little phenagling which allows us to use lambdas instead.
         * */

        connect(motorKill, &QTimer::timeout, 
                [this] () {performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);});

        connect(ui.reset_starting_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::RESET_STARTING);});
        connect(ui.reset_dumping_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::RESET_DUMPING);});
        connect(ui.dump_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::DUMP);});
        connect(ui.dig_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::DIG);});

        /* for commands which do the turntable/drivebase we want to kill the
         * motors after release*/
        // arm buttons
        //lower arm
        connect(ui.lower_arm_extend_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::LOWER_ARM_EXTEND);}); 
        connect(ui.lower_arm_retract_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::LOWER_ARM_RETRACT);});
        //upper arm        
        connect(ui.upper_arm_extend_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::UPPER_ARM_EXTEND);}); 
        connect(ui.upper_arm_retract_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::UPPER_ARM_RETRACT);});
        //scoop        
        connect(ui.scoop_extend_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::SCOOP_EXTEND);}); 
        connect(ui.scoop_retract_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::SCOOP_RETRACT);}); 
        // raise arm to straight, neutral position above robot
        connect(ui.raise_arm_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::RAISE_ARM);});
        //turntable 
        connect(ui.cw_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::CLOCKWISE);});
        connect(ui.ccw_button,&QPushButton::clicked,
                [this] () {performTeleop(tfr_utilities::TeleopCode::COUNTERCLOCKWISE);});
        //drivebase
        //forward
        connect(ui.forward_button,&QPushButton::pressed,
                [this] () {performTeleop(tfr_utilities::TeleopCode::FORWARD);});
        connect(ui.forward_button,&QPushButton::released,
                [this] () {performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);});
        //backward
        connect(ui.backward_button,&QPushButton::pressed,
                [this] () {performTeleop(tfr_utilities::TeleopCode::BACKWARD);});
        connect(ui.backward_button,&QPushButton::released,
                [this] () {performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);});
        //left
        connect(ui.left_button,&QPushButton::pressed,
                [this] () {performTeleop(tfr_utilities::TeleopCode::LEFT);});
        connect(ui.left_button,&QPushButton::released,
                [this] () {performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);});
        //right
        connect(ui.right_button,&QPushButton::pressed,
                [this] () {performTeleop(tfr_utilities::TeleopCode::RIGHT);});
        connect(ui.right_button,&QPushButton::released,
                [this] () {performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);});    
        

        //set upp our action servers
        ROS_INFO("Mission Control: connecting autonomy");
        autonomy.waitForServer();
        ROS_INFO("Mission Control: connected autonomy");
        ROS_INFO("Mission Control: connecting teleop");
        teleop.waitForServer();
        ROS_INFO("Mission Control: connected teleop");
    }

    /*
     * This get's called before the destructor, and apparently we need to
     * deallocate our ros objects manually here according to the documentation.
     * Publishers and subscribers especially
     * */
    void MissionControl::shutdownPlugin()
    {
        //note because qt plugins are weird we need to manually kill ros entities
        com.shutdown();
        autonomy.cancelAllGoals();
        autonomy.stopTrackingGoal();
        teleop.cancelAllGoals();
        teleop.stopTrackingGoal();
    }

    /* ========================================================================== */
    /* Settings                                                                   */
    /* ========================================================================== */

    /* we inherit this, but I don't think we need it*/ 
    void MissionControl::saveSettings(
            qt_gui_cpp::Settings& plugin_settings,
            qt_gui_cpp::Settings& instance_settings) const
    {
    }

    /* we inherit this, but I don't think we need it*/ 
    void MissionControl::restoreSettings(
            const qt_gui_cpp::Settings& plugin_settings,
            const qt_gui_cpp::Settings& instance_settings)
    {
    }

    /* ========================================================================== */
    /* Methods                                                                    */
    /* ========================================================================== */
 
    /*
     * Startup utility to reset the turntable
     * */
    void MissionControl::resetTurntable()
    {
        ROS_INFO("Mission Control: Resetting turntable");
        toggleMotors(false);
        std_srvs::Empty::Request req;
        std_srvs::Empty::Response res;
        while(!ros::service::call("/zero_turntable", req, res))
            ros::Duration{0.1}.sleep();

	performTeleop(tfr_utilities::TeleopCode::DRIVING_POSITION);
        toggleMotors(true);

    }
   
    /* greys/ungreys all teleop buttons, and tell's system whether to process teleop or
     * not
     * */
    void MissionControl::setTeleop(bool value)
    {
        ui.left_button->setEnabled(value);
        ui.right_button->setEnabled(value);
        ui.forward_button->setEnabled(value);
        ui.backward_button->setEnabled(value);
        ui.autonomy_button->setEnabled(value);
        ui.dump_button->setEnabled(value);
        ui.dig_button->setEnabled(value);
        ui.lower_arm_extend_button->setEnabled(value);
        ui.lower_arm_retract_button->setEnabled(value);
        ui.upper_arm_extend_button->setEnabled(value);
        ui.upper_arm_retract_button->setEnabled(value);
        ui.scoop_extend_button->setEnabled(value);
        ui.scoop_retract_button->setEnabled(value);
        teleopEnabled = value;
    }

    /*
     * Toggles the autonomy stop button
     * */
    void MissionControl::setAutonomy(bool value)
    {
        ui.teleop_button->setEnabled(value);
    }

    /*
     * Toggles the control buttons
     * */
    void MissionControl::setControl(bool value)
    {
        ui.control_enable_button->setEnabled(!value);
        ui.control_disable_button->setEnabled(value);
    }

    /*
     * Toggles the motor buttons
     * */
    void MissionControl::setMotors(bool value)
    {
        ui.motor_enable_button->setEnabled(!value);
        ui.motor_disable_button->setEnabled(value);
    }
    
    /*
     * Utility for stopping all motors
     * */
    void MissionControl::softwareStop()
    {
        performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);
        teleop.waitForResult();
    }

    /* ========================================================================== */
    /* Events                                                                     */
    /* ========================================================================== */
    /*
     * Processes key events, if we don't consume the event, we need to pass it
     * on to the other filters. Note I do time based debouncing here, with a
     * watchdog.
     *
     * The problem is repeated keypresses, and the debouncing in qt being
     * rubbish. 
     *
     * So when we get a valid key in, I start a watchdog for the motors.
     * Whenever we get a repeat of that key I ignore it and restart the
     * watchdog. When the watchdog expires it stops the drivebase
     *
     * This can cause problems if the user has a long key delay (gt .3
     * seconds)
     *
     * I can't set the watchdog too long, or else there is an unacceptable delay
     * in stopping the motors. So I make it a requirement that our laptop have a
     * reasonable key delay set. 
     *
     * QT provides some built in functionality for this, but it's rubbish, and I
     * couldn't get fine enough control to meet our response requirements.
     * */
    bool MissionControl::eventFilter(QObject* obj, QEvent* event)
    {
        if (event->type()==QEvent::KeyPress && teleopEnabled) {
            QKeyEvent* key = static_cast<QKeyEvent*>(event);
            auto  k = static_cast<Qt::Key>(key->key());

            switch(k)
            {
                //process the key and start watchdog
                case (Qt::Key_W):
                    motorKill->start(MOTOR_INTERVAL);
                    performTeleop(tfr_utilities::TeleopCode::FORWARD);
                    break;
                case (Qt::Key_S):
                    motorKill->start(MOTOR_INTERVAL);
                    performTeleop(tfr_utilities::TeleopCode::BACKWARD);
                    break;
                case (Qt::Key_A):
                    motorKill->start(MOTOR_INTERVAL);
                    performTeleop(tfr_utilities::TeleopCode::LEFT);
                    break;
                case (Qt::Key_D):
                    motorKill->start(MOTOR_INTERVAL);
                    performTeleop(tfr_utilities::TeleopCode::RIGHT);
                    break;
                case (Qt::Key_Space):
                    performTeleop(tfr_utilities::TeleopCode::STOP_DRIVEBASE);
                    break;
                case (Qt::Key_U):
                    performTeleop(tfr_utilities::TeleopCode::LOWER_ARM_EXTEND);
                    break;
                case (Qt::Key_J):
                    performTeleop(tfr_utilities::TeleopCode::LOWER_ARM_RETRACT);
                    break;
                case (Qt::Key_I):
                    performTeleop(tfr_utilities::TeleopCode::UPPER_ARM_EXTEND);
                    break;
                case (Qt::Key_K):
                    performTeleop(tfr_utilities::TeleopCode::UPPER_ARM_EXTEND);
                    break;
                case (Qt::Key_O):
                    performTeleop(tfr_utilities::TeleopCode::SCOOP_EXTEND);
                    break;
                case (Qt::Key_L):
                    performTeleop(tfr_utilities::TeleopCode::SCOOP_RETRACT);
                    break;
            }
            //consume the key
            return true;
        }
        else {
            //unrecognized event type, pass it on
            return QObject::eventFilter(obj, event);
        }
    }

    /* ========================================================================== */
    /* Callbacks                                                                  */
    /* ========================================================================== */
    /*
     * Callback for our status subscriber this happens in the "ros" thread so I
     * need to communicate to the GUI in a safe signal/slot combination.
     *
     * This triggers our custom emitStatus signal, which triggers the built in
     * text update in the text box, a seperate timer event is constantly
     * scrolling the window on a set fast interval.
     * */
    void MissionControl::updateStatus(const tfr_msgs::SystemStatusConstPtr &status)
    {
        auto str = getStatusMessage(static_cast<StatusCode>(status->status_code), status->data);
        //need to wrap in signal safe "Q" object
        QString msg = QString::fromStdString(str);
        emit emitStatus(msg);
    }

    /* ========================================================================== */
    /* Slots                                                                      */
    /* ========================================================================== */

    //self explanitory, starts the time service in executive
    void MissionControl::startTimeService()
    {
        std_srvs::Empty start;
        ros::service::call("start_mission", start);
        //start updating the gui mission clock
        countdownClock->start(500);
    }

    //starts mission in autonomous mode
    void MissionControl::startMission()
    {
        startTimeService();
        goAutonomousMode();
        toggleControl(true);
        toggleMotors(true);
    }
    
    //starts mission is teleop mode
    void MissionControl::startManual()
    {
        startTimeService();
        goTeleopMode();
        toggleControl(true);
        toggleMotors(true);
    }

    //triggers state change into autonomous mode from teleop
    void MissionControl::goAutonomousMode()
    {
        resetTurntable();
        softwareStop();
        setAutonomy(true);
        tfr_msgs::EmptyGoal goal{};
        while (!teleop.getState().isDone()) teleop.cancelAllGoals();
        autonomy.sendGoal(goal);
        setTeleop(false);
        widget->setFocus();

    }

    //triggers state change into from teleop into autonomy
    void MissionControl::goTeleopMode()
    {
        setAutonomy(false);
        while (!autonomy.getState().isDone()) autonomy.cancelAllGoals();
        //resetTurntable();
        setTeleop(true);
        softwareStop();
        widget->setFocus();
    }

    //performs a teleop command asynchronously 
    void MissionControl::performTeleop(tfr_utilities::TeleopCode code)
    {
        tfr_msgs::TeleopGoal goal;
        goal.code = static_cast<uint8_t>(code);
        teleop.sendGoal(goal);
    }

    //toggles control for estop (on/off)
    void MissionControl::toggleControl(bool state)
    {
        std_srvs::SetBool request;
        request.request.data = state;
        while(!ros::service::call("toggle_control", request));
        setControl(state);
    }

    //toggles control for estop (on/off)
    void MissionControl::toggleMotors(bool state)
    {
        std_srvs::SetBool request;
        request.request.data = state;
        while(!ros::service::call("toggle_motors", request));
        setMotors(state);
    }

    //self explanitory
    void MissionControl::renderClock()
    {
        tfr_msgs::DurationSrv remaining_time;
        ros::service::call("time_remaining", remaining_time);
        ui.time_display->display(remaining_time.response.duration.toSec());
    }

    //scrolls the status window
    void MissionControl::renderStatus()
    {
        ui.status_log->verticalScrollBar()->setValue(ui.status_log->verticalScrollBar()->maximum());
    }




    /* ========================================================================== */
    /* Signals                                                                    */
    /* ========================================================================== */


} // namespace

PLUGINLIB_EXPORT_CLASS(tfr_mission_control::MissionControl,
        rqt_gui_cpp::Plugin)
