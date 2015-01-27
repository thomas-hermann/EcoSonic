#include "stdafx.h"
#include "qcarviz.h"
#include "speed_observer.h"
#include "logging.h"

#define DRAW_ARROW_SIGN 2 // 0: simple arrow 1: arrow sign 2: "street"-arrow 3: curved "street"-arrow
#define SHOW_EYETRACKER_POINT

QCarViz::QCarViz(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    QPalette p = palette();
    p.setColor(backgroundRole(), Qt::white);
    setPalette(p);
    flash_timer.start();

    qDebug() << QDir::currentPath();
    //printf("%s\n", QDir::currentPath().toStdString().c_str());

    add_tree_type("tree", 0.2/3, 50*3);
    add_tree_type("birch", 0.08, 140);
    add_tree_type("spooky_tree", 0.06, 120);

    car_img.load("media/cars/car.png");
    turn_sign.reset(new QSvgRenderer(QString("media/turn_sign.svg")));
    turn_sign_rect = QRectF(QPointF(0, 0), 0.03 * turn_sign->defaultSize());
    track.load();
    update_track_path(height());
    fill_trees();
    tick_timer.setInterval(30); // minimum simulation interval
    QObject::connect(&tick_timer, SIGNAL(timeout()), this, SLOT(tick()));
}

void QCarViz::init(Car* car, QPushButton* start_button, QSlider* throttle, QSlider* breaking, QSpinBox* gear, QMainWindow* main_window, OSCSender* osc, bool start)
{
    this->car = car;
    this->start_button = start_button;
    throttle_slider = throttle;
    breaking_slider = breaking;
    gear_spinbox = gear;
    speedObserver.reset(new SpeedObserver(*this, *osc));
    turnSignObserver.reset(new TurnSignObserver(*this));
    QObject::connect(start_button, SIGNAL(clicked()),
                     this, SLOT(start_stop()));
    keyboard_input.init(main_window);
    consumption_monitor.osc = osc;
    this->osc = osc;

    program_start_time = QDateTime::currentDateTime();
    connect(&eye_tracker_socket, &QTcpSocket::connected, this, &QCarViz::eye_tracker_connected);
    connect(&eye_tracker_socket, &QTcpSocket::disconnected, this, &QCarViz::eye_tracker_disconnected);
    connect(&eye_tracker_socket, SIGNAL(error(QAbstractSocket::SocketError)), this, SLOT(eye_tracker_error(QAbstractSocket::SocketError)));
    connect(&eye_tracker_socket, &QTcpSocket::readyRead, this, &QCarViz::eye_tracker_read);
    if (start)
        QTimer::singleShot(500, this, SLOT(start()));
}

void QCarViz::copy_from_track_editor(QTrackEditor* track_editor)
{
    track = track_editor->track;
    prepare_track();
}

void QCarViz::trigger_arrow()
{
    std::uniform_int_distribution<int> dir(0,1);
    std::uniform_real_distribution<qreal> intensity(0.3, 1);
    std::uniform_real_distribution<qreal> duration(0.8, 1.5);
    std::uniform_real_distribution<qreal> fade_in(0.05, 0.5);
    std::uniform_real_distribution<qreal> fade_out(0.05, 0.5);
    static Track::Sign sign;
    sign = Track::Sign(!dir(rng) ? Track::Sign::TurnLeft : Track::Sign::TurnRight, 0);
    turnSignObserver->trigger(&sign, time_elapsed());
}

void QCarViz::start() {
    if (current_pos >= track_path.length()) {
        current_pos = initial_pos;
        car->gearbox.clutch.disengage();
        if (!replay)
            car->engine.reset();
        car->gearbox.reset();
        car->speed = 0;
        consumption_monitor.reset();
        if (!replay)
            track_started = false;
        for (Track::Sign& s : track.signs) {
            if (s.type == Track::Sign::TrafficLight)
                s.traffic_light_state = Track::Sign::Red;
        }
        turnSignObserver->reset();
    }
    time_delta.start();
    tick_timer.start();
    started = true;
    osc->send_float("/startEngine", 0);
    start_button->setText("Pause");
}

bool QCarViz::load_log(const QString filename) {
    std::shared_ptr<Log>& log = car->log;
    log.reset(new Log(car, this, &track));
    const bool ret = loadObj(filename, *log);
    if (ret) {
        if (!log->valid) {
            qDebug() << "wrong version!";
            //printf("wrong version!\n");
            return false;
        }
        printf("log: elapsed_time: %.3f\n", log->elapsed_time);
        printf("log: deciliters_used: %.4f\n", log->liters_used * 10);
//        printf("log: items: %i\n", log->items.size());
        this->sound_modus = log->sound_modus;
        prepare_track();
        replay = true;
        replay_index = 0;
        track_started = true;
        track_started_time = time_delta.get_elapsed();
        car->engine.angular_velocity = log->initial_angular_velocity;
        printf("log: initial rpm: %.3f\n", car->engine.rpm());
        current_pos = track_path.length();
        start();
        update_sound_modus(sound_modus);
    }
    return ret;
}

void QCarViz::prepare_track() {
    track.prepare_track();
    update_track_path(height());
    speedObserver->tick();
    turnSignObserver->reset();
    fill_trees();
}

bool QCarViz::tick() {
    Q_ASSERT(started);
    qreal dt;
    static bool changed = false; // if throttle / gas have changed (for the sliders in the gui)
    if (replay == true) {
        if (replay_index >= car->log->items.size()) {
            //printf("elapsed_time: %.3f\n", time_delta.get_elapsed() - track_started_time);
            //printf("deciliters_used: %.4f\n", consumption_monitor.liters_used * 10);
            stop();
            return false;
        }
        LogItem& log_item = car->log->items[replay_index];
        dt = log_item.dt;
        car->braking = log_item.braking;
        car->gearbox.set_gear(log_item.gear);
        car->throttle = log_item.throttle;
        changed = true;
        replay_index++;
        time_delta.add_dt(dt);
    } else {
        if (!time_delta.get_time_delta(dt))
            return false;

        // keyboard input
        if (keyboard_input.update()) {
            car->throttle = keyboard_input.throttle();
            car->braking = keyboard_input.breaking();
            changed = true;
        }
        const int gear_change = keyboard_input.gear_change();
        if (gear_change) {
            if (gear_change > 0)
                car->gearbox.gear_up();
            else if (gear_change < 0)
                car->gearbox.gear_down();
            gear_spinbox->setValue(car->gearbox.get_gear()+1);
        }
        if (keyboard_input.show_arrow()) {
            //qDebug() << "trigger arrow";
            trigger_arrow();
        }
        if (keyboard_input.keys_pressed.contains(Qt::Key_O)) // steering to the left
            steer(dt); // "reduces" steering hint
        if (keyboard_input.keys_pressed.contains(Qt::Key_P))
            steer(-dt);

//    // manual clutch control
//    const bool toggle_clutch = keyboard_input.toggle_clutch();
//    if (toggle_clutch) {
//        Clutch& clutch = car->gearbox.clutch;
//        if (clutch.engage)
//            clutch.disengage();
//        else
//            clutch.clutch_in(car->engine, &car->gearbox, car->speed);
//    }
    }
    qreal t = time_elapsed();

    // sound modus (supercollider)
    update_sound_modus(keyboard_input.get_sound_modus());
    // control window
    if (keyboard_input.show_control_window() && (sound_modus == 2 || sound_modus == 3))
        osc->call("/show_controls");
    // toggle pitch control (for grains)
    if (keyboard_input.pitch_toggle() && sound_modus == 3)
        osc->call("/grain_toggle_pitch");
    // volume control
    if (keyboard_input.vol_up())
        osc->call("/vol_up");
    else if (keyboard_input.vol_down())
        osc->call("/vol_down");

    if (!replay) {
        // Wingman input
        if (wingman_input.valid()) {
            if (wingman_input.update()) {
                car->throttle = wingman_input.gas();
                car->braking = wingman_input.brake();
                changed = true;
            }
            if (wingman_input.update_buttons()) {
                if (wingman_input.left_click())
                    car->gearbox.gear_down();
                if (wingman_input.right_click())
                    car->gearbox.gear_up();
                gear_spinbox->setValue(car->gearbox.get_gear()+1);
            }
            wingman_input.update_wheel();
            if (!wingman_input.wheel_neutral()) {
                steer(-wingman_input.wheel() * dt * 1.5);
            }
        }
        if (!track_started && car->throttle > 0) {
            track_started = true;
            track_started_time = time_delta.get_elapsed();
            car->log.reset(new Log(car, this, &track));
            car->log->initial_angular_velocity = car->engine.angular_velocity;
        }
    }
    turnSignObserver->tick(t, dt);

    // automatic clutch control
    if (track_started)
        car->gearbox.auto_clutch_control(car);

    const qreal alpha_scale = 0.8;
    const qreal alpha = !track_path.length() ? 0 : (alpha_scale * atan(-track_path.slopeAtPercent(track_path.percentAtLength(current_pos)))); // slope [rad]
    Q_ASSERT(!isnan(alpha));
    car->tick(dt, alpha, replay);
    if (track_started) {
        consumption_monitor.tick(car->engine.get_consumption_L_s(), dt, car->speed);
    } else {
        Q_ASSERT(!replay);
        car->speed = 0;
    }
    current_pos += car->speed * dt * 3;
    if (!replay && track_path.length() > initial_pos && current_pos >= track_path.length()) {
        stop();
		Q_ASSERT(car->log != nullptr);
        car->log->elapsed_time = time_elapsed();
        car->log->liters_used = consumption_monitor.liters_used;
        car->log->sound_modus = sound_modus;
        car->save_log(program_start_time);
    }
    double l_100km;
    if (consumption_monitor.get_l_100km(l_100km, car->speed)) {
        hud.l_100km = l_100km;
        //printf("%.3f\n", l_100km);
    }

    static qreal last_elapsed = 0;
    qreal elapsed = time_delta.get_elapsed();
    if (elapsed - last_elapsed > 0.05) {
        if (changed) {
            throttle_slider->setValue(car->throttle * 100);
            breaking_slider->setValue(car->braking * 100);
            changed = false;
        } else {
            car->throttle = throttle_slider->value() / 100.;
            car->braking = breaking_slider->value() / 100.;
        }
        if (keyboard_input.connect()) {
            connect_to_eyetracker();
        }
        speedObserver->tick();
        emit slow_tick(elapsed - last_elapsed, elapsed, consumption_monitor);
        last_elapsed = elapsed;
    }
    return true;
}

void QCarViz::draw(QPainter& painter)
{
    if (flash_timer.elapsed() < 100)
        return;
    QTransform t;
    const qreal current_percent = track_path.percentAtLength(current_pos);
    const qreal car_x_pos = 50;

    const QPointF cur_p = track_path.pointAtPercent(current_percent);
    //printf("%.3f\n", current_alpha * 180 / M_PI);

    //draw the current speed limit / current_pos / time elapsed
    QString number; number.sprintf("%04.1f", time_elapsed());
    painter.setFont(QFont{"Eurostile", 18, QFont::Bold});
    QPointF p = {300,300};
    if (track_started)
        painter.drawText(p, number);
    // /////

    // draw remaining time
    TimeDisplay::draw(painter, {10,30}, track.max_time, time_elapsed(), track_started);

    const bool hud_external = hud_window.get() != nullptr;

    // draw the HUD speedometer & revcounter
    if (hud_external) {
        QHudWidget& hw = hud_window->hud_widget();
        hw.update_hud(&hud, car->engine.rpm(), Gearbox::speed2kmh(car->speed), consumption_monitor.liters_used);
    } else {
        t.translate(0, 0.75 * height() - 0.5 * hud.height);
        painter.setTransform(t);
        hud.draw(painter, width(), car->engine.rpm(), Gearbox::speed2kmh(car->speed), consumption_monitor.liters_used); //, track.max_time, time_delta.get_elapsed() - track_started_time, track_started);
    }
    // draw the road
    painter.setPen(QPen(Qt::black, 1));
    //painter.drawLine(0, height()/2, width(), height()/2);
    painter.setBrush(Qt::NoBrush);
    QTransform t0;
    t0.translate(0, (hud_external ? -0.25 : -0.5) * height());
    t = t0;
    t.translate(car_x_pos - cur_p.x(),0);
    painter.setTransform(t);
    painter.drawPath(track_path);

    // draw the signs
    for (int i = 0; i < track.signs.size(); i++) {
        track.signs[i].draw(painter, track_path);
    }

    // draw the car
    const qreal car_width = 60.;
    const qreal car_height = (qreal) car_img.size().height() / car_img.size().width() * car_width;

    t = t0;
    t.translate(car_x_pos, cur_p.y());
    //t.rotateRadians(atan(slope));
    t.rotate(-track_path.angleAtPercent(current_percent));
    t.translate(-car_width/2, -car_height);
    painter.setTransform(t);
    painter.drawImage(QRectF(0,0, car_width, car_height), car_img);

    // draw the trees in the foreground
    t = t0;
    t.translate(car_x_pos - cur_p.x(),0);
    painter.setTransform(t);

    const qreal track_bottom = track_path.boundingRect().bottom();
    for (Tree tree : trees) {
        const qreal tree_x = tree.track_x(cur_p.x());
        if (tree_x > size().width() + cur_p.x() + 200)
            continue;
        QPointF tree_pos(tree_x, track_bottom);
        tree_types[tree.type].draw_scaled(painter, tree_pos, Gearbox::speed2kmh(car->speed), tree.scale);
    }

    // draw an arrow
    //printf("%s ", show_arrow == Arrow::None ? "None" : (show_arrow == Arrow::Left ? "Left" : "Right"));
#if (DRAW_ARROW_SIGN < 2)
    const double center_tolerance = 0.05;
    if (steering > center_tolerance || steering < -center_tolerance)
#endif
    {
        //qDebug() << steering;
        //printf("draw ");
        //t = t0;
        t.reset();

#if (DRAW_ARROW_SIGN==0)
        t.translate(0.5 * width(), 40);
        painter.setOpacity(fabs(steering) - center_tolerance);
#elif (DRAW_ARROW_SIGN==1)
        t.translate(0.5 * (width() - turn_sign_rect.width()), 20);
        if (steering < 0) {
            t.scale(-1,1);
            t.translate(-turn_sign_rect.width(),0);
        }
        painter.setOpacity(fabs(steering) - center_tolerance);
#else
        t.translate(0.5 * width(), 20);
#endif
        painter.setTransform(t);
#if (DRAW_ARROW_SIGN==0)
        const qreal mult = steering < 0 ? -1 : 1;
        const qreal head_x = fabs(steering) * mult * 30;
        const QPointF head(head_x, 0);
//        painter.drawPoint(QPointF(0,0));
//        painter.drawPoint(head);
        painter.setPen(QPen(Qt::black, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPointF(0,0), head);
        painter.drawLine(QPointF(head_x - 5*mult, -5), head);
        painter.drawLine(QPointF(head_x - 5*mult, +5), head);
#elif (DRAW_ARROW_SIGN==1)
        turn_sign->render(&painter, turn_sign_rect);
#elif (DRAW_ARROW_SIGN==2)
        const qreal scale = 80;
        const qreal alpha = steering;
        //const qreal a = 0.5*M_PI + alpha;
        //QPointF top(cos(a), 1 - 0.4 - 0.4 * sin(a)); top *= scale;
        QPointF top(alpha, 0.5); top *= scale;
        QPointF left(-0.4, 1); left *= scale;
        QPointF right(0.4, 1); right *= scale;
        QPointF mid(0, 1 * scale);
        painter.drawLine(top, left);
        painter.drawLine(top, right);
        QPainterPath p; p.moveTo(top); p.lineTo(left); p.lineTo(right); p.lineTo(top);
        painter.fillPath(p, Qt::gray);
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        pen.setDashOffset(-current_pos * 0.2);
        painter.setPen(pen);
        painter.drawLine(top, mid);
#else
        const qreal scale = 80;
        QPointF top(-steering, 0.5); top *= scale;
        QPointF left(-0.4, 1); left *= scale;
        QPointF right(0.4, 1); right *= scale;
        QPointF mid(0, 1 * scale);
        painter.drawLine(top, left);
        painter.drawLine(top, right);
        QPainterPath p; p.moveTo(top); p.lineTo(left); p.lineTo(right); p.lineTo(top);
        painter.fillPath(p, Qt::gray);
        QPen pen(Qt::white);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(top, mid);
#endif
        painter.setOpacity(2.0);
    }
#ifdef SHOW_EYETRACKER_POINT
    //eye_tracker_point = QCursor::pos();
    //globalToLocalCoordinates(eye_tracker_point);
    painter.setTransform(QTransform());
    painter.setBrush(Qt::NoBrush);
    painter.setPen(Qt::black);
    painter.drawEllipse(eye_tracker_point, 10, 10);
#endif
}
