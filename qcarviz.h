#ifndef QCARVIZ_H
#define QCARVIZ_H

#include <QWidget>
#include <QImage>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QThread>
#include <QElapsedTimer>
#include <string>
#include <QTimer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>
#include <QShortcut>
#include <QMainWindow>
#include <QSvgGenerator>
#include <QTcpSocket>
#include <boost/algorithm/clamp.hpp>
#include <algorithm>
#include <random>
#include "hud.h"
#include "qtrackeditor.h"
#include "wingman_input.h"
#include "KeyboardInput.h"
#include "misc.h"
#include "track.h"
#include "car.h"
#include "hudwindow.h"

#include <QMessageBox>
#include <QLabel>
#include <QMenu>



#define DEFAULT_SPEED_LIMIT 300 // kmh

static std::mt19937_64 rng(std::random_device{}());

class SignObserverBase;
class TurnSignObserver;
//struct SpeedObserver;
class QCarViz;

enum TrafficViolation {
    Speeding,
    StopSign,
    TrafficLight,
};

struct EyeTrackerClient : public QThread
{
    Q_OBJECT

    typedef QAbstractSocket::SocketError SocketError;

signals:

    void destroyed();

public:
    EyeTrackerClient(QCarViz* car_viz)
        : car_viz(car_viz)
    { }

    void disconnect() {
        quit = true;
    }

    void run() override {
        connect(this, &QThread::finished, this, &EyeTrackerClient::finish);
        socket.reset(new QTcpSocket());
        connect(socket.get(), &QTcpSocket::disconnected, this, &EyeTrackerClient::disconnected);
        connect(socket.get(), static_cast<void (QTcpSocket::*)(QAbstractSocket::SocketError)> (&QTcpSocket::error),
                [&](QAbstractSocket::SocketError error)
        {
            if (!(socket->isOpen() && error == QAbstractSocket::SocketTimeoutError))
            qDebug() << "eye Tracker Socket ERROR:" << error;
        });
            //, &EyeTrackerClient::tcp_error(SocketAccessError));

        qDebug() << "connecting...";
        //socket->connectToHost("127.0.0.1", 7767);
        socket->connectToHost("192.168.0.10", 7767);
        if (!socket->waitForConnected(2000)) {
            qDebug() << "connecting failed!";
            return;
        }
        while (!quit) {
            if (socket->waitForReadyRead(100))
                read();
        }
        socket->abort();
        socket->close();
        qDebug() << "stopping eye tracker thread";
    }

//    void destroy() {

//    }

protected slots:
    void disconnected() {
        qDebug() << "eye Tracker Socket disconnected";
        quit = true;
    }

    void finish() {
        emit destroyed();
        deleteLater();
    }

protected:
    void read();

    QCarViz* car_viz;
    //QTcpSocket socket;
    std::auto_ptr<QTcpSocket> socket;
    bool first_read = true;
    bool quit = false;
};

class QCarViz : public QWidget
{
    Q_OBJECT

public:
    friend class SignObserverBase;

    QCarViz(QWidget *parent = 0);

    virtual ~QCarViz() {
        if (eye_tracker_client != nullptr) {
            eye_tracker_client->disconnect();
            eye_tracker_client->wait(500);
        }
    }

    void init(Car* car, QPushButton* start_button, QSlider* throttle, QSlider* breaking, QSpinBox* gear, QMainWindow* main_window, OSCSender* osc, bool start = true);

    void copy_from_track_editor(QTrackEditor* track_editor);

    void update_sound_modus(int const sound_modus) {
        if (sound_modus != this->sound_modus) {
            if (this->sound_modus == 1)
                osc->call("/slurp_stop");
            else if (this->sound_modus == 2)
                osc->call("/pitch_stop");
            else if (this->sound_modus == 3)
                osc->call("/grain_stop");

            if (sound_modus == 1)
                osc->call("/slurp_start");
            else if (sound_modus == 2)
                osc->call("/pitch_start");
            else if (sound_modus == 3)
                osc->call("/grain_start");
            this->sound_modus = sound_modus;
        }
    }

    bool load_log(const QString filename);

    std::auto_ptr<HUDWindow> hud_window;

public slots:
    void stop(bool temporary_stop = false) {
        tick_timer.stop();
        started = false;
        if (!temporary_stop) {
            osc->call("/stopEngine");
            update_sound_modus(0);
        }
        start_button->setText("Cont.");
        //save_svg();
    }

    void start();

signals:
    void slow_tick(qreal dt, qreal elapsed, ConsumptionMonitor& consumption_monitor);

protected slots:

    void start_stop() {
        started ? stop() : start();
    }

    bool tick();

public:

    void globalToLocalCoordinates(QPointF &pos) const
    {
        const QWidget* w = this;
        while (w) {
            pos.rx() -= w->geometry().x();
            pos.ry() -= w->geometry().y();
            w = w->isWindow() ? 0 : w->parentWidget();
        }
    }

    void set_eye_tracker_point(QPointF& p) {
        // could me made thread-safe..
        if (!replay) {
            eye_tracker_point = p;
            t_last_eye_tracking_update = time_elapsed();
        }
    }

    void traffic_violation(const TrafficViolation violation) {
        osc->send_float("/flash", 0);
        flash_timer.start();
    }

    QPointF& get_eye_tracker_point() { return eye_tracker_point; }

    qreal get_kmh() { return Gearbox::speed2kmh(car->speed); }
    qreal get_user_steering() { return user_steering; }

    void steer(const qreal val) {
        steering = boost::algorithm::clamp(steering + val, -1, 1);
        //qDebug() << val << steering;
    }

    const Car* get_car() const { return car; }
    qreal get_current_pos() const { return current_pos; }

    Track track;
    QElapsedTimer flash_timer; // controls the display of a flash (white screen)

protected:

    void fill_trees() {
        trees.clear();
        const qreal first_distance = 40; // distance from starting position of the car
        const qreal track_length = track_path.boundingRect().width();
        std::uniform_int_distribution<int> tree_type(0,tree_types.size()-1);
        std::uniform_real_distribution<qreal> dist(5,50); // distance between the trees
        const qreal first_tree = track_path.pointAtPercent(track_path.percentAtLength(initial_pos)).x() + first_distance;
        const qreal scale = 5;

        for (double x = first_tree; x < track_length; x += dist(rng)) {
            trees.append(Tree(tree_type(rng), x, scale, 10*scale));
        }
        //printf("%.3f\n", trees[0].pos);
    }

    void prepare_track();

    void save_svg() {
        QSvgGenerator generator;
        generator.setFileName("/Users/jhammers/car_simulator.svg");
        const QSize size = this->size();
        generator.setSize(size);
        generator.setViewBox(QRect(0, 0, size.width(), size.height()));

        QPainter painter;
        painter.begin(&generator);
        draw(painter);
        painter.end();
    }

    void trigger_arrow();
    qreal time_elapsed() {
        return time_delta.get_elapsed() - track_started_time;
    }

    void connect_to_eyetracker() {
        if (eye_tracker_client != nullptr) {
            eye_tracker_client->disconnect();
            eye_tracker_client = nullptr;
        } else {
            eye_tracker_client = new EyeTrackerClient(this);
            connect(eye_tracker_client, &EyeTrackerClient::destroyed, [this](){
                qDebug() << "eye_tracker_client = nullptr";
                eye_tracker_client = nullptr;
            });
            eye_tracker_client->start();
        }
//        if (eye_tracker_socket.isOpen()) {
//            qDebug() << "disconnecting...";
//            eye_tracker_socket.abort();
//            eye_tracker_socket.close();
//        } else {
//            qDebug() << "connecting...";
//            eye_tracker_socket.connectToHost("127.0.0.1", 7767);
////            eye_tracker_socket.connectToHost("192.168.0.10", 7767);
//            eye_tracker_socket.waitForConnected(2000);
//            //eye_tracker_socket.waitForReadyRead(2000);
//        }
    }

    void draw(QPainter& painter);

    virtual void paintEvent(QPaintEvent *) {
//        static FPSTimer fps("paint: ");
//        fps.addFrame();
        if (started)
            tick();

        QPainter painter(this);
        draw(painter);

        if (started) {
            update();
        }
    }

    void add_tree_type(const QString name, const qreal scale, const qreal y_offset) {
        const QString path = "media/trees/" + name;
        tree_types.append(TreeType(path + "_base.png", scale, y_offset));
        tree_types.last().add_speedy_image(path + "1", 10);
        tree_types.last().add_speedy_image(path + "2", 30);
        tree_types.last().add_speedy_image(path + "3", 50);
        tree_types.last().add_speedy_image(path + "4", 70);
        tree_types.last().add_speedy_image(path + "5", 100);
    }

    void update_track_path(const int height) {
        QPainterPath path;
        track.get_path(path, height);
        track_path.swap(path);
    }

    virtual void resizeEvent(QResizeEvent *e) {
        update_track_path(e->size().height());
    }

    const qreal initial_pos = 40;
    qreal current_pos = initial_pos; // current position of the car. max is: track_path.length()
    QImage car_img;
    //std::auto_ptr<SpeedObserver> speedObserver;
    std::vector<SignObserverBase*> signObserver;
    TurnSignObserver* turnSignObserver = nullptr;
//    std::auto_ptr<TurnSignObserver> turnSignObserver;
//    std::auto_ptr<StopSignObserver> stopSignObserver;
    TimeDelta time_delta;
    Car* car;
    QPainterPath track_path;
    QVector<TreeType> tree_types;
    QVector<Tree> trees;
    bool started = false;
    QTimer tick_timer; // for simulation-ticks if the window is not visible
    QPushButton* start_button = NULL;
    QSlider* throttle_slider = NULL;
    QSlider* breaking_slider = NULL;
    QSpinBox* gear_spinbox = NULL;
    WingmanInput wingman_input;
    KeyboardInput keyboard_input;
    ConsumptionMonitor consumption_monitor;
    HUD hud;
    bool track_started = false;
    qreal track_started_time = 0;
    OSCSender* osc = NULL;
    int sound_modus = 0;
    QDateTime program_start_time;
    bool replay = false;
    int replay_index = 0;
    std::auto_ptr<QSvgRenderer> turn_sign;
    QRectF turn_sign_rect;
    qreal steering = 0; // between -1 (left) and 1 (right)
    qreal user_steering = 0;
//    qreal turn_sign_length = 0;
//    qreal current_turn_sign_length = 0;
    QPointF eye_tracker_point;
    qreal t_last_eye_tracking_update = 0;
//    QTcpSocket eye_tracker_socket;
//    bool first_read = true;
    EyeTrackerClient* eye_tracker_client = nullptr;
};


#endif // QCARVIZ_H
