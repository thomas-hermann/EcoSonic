#ifndef TRACK_H
#define TRACK_H

#include <QPainterPath>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>

struct TreeType {
    struct SpeedyImage {
        QImage img;
        qreal kmh;
    };

    TreeType() {}
    TreeType(QString path, const qreal scale, const qreal y_offset)
        : scale(scale)
        , y_offset(y_offset)
    {
        img.load(path);
    }
    void draw_scaled(QPainter& painter, const QPointF pos, qreal kmh, qreal scale = 1) const {
        scale *= this->scale;
        QSizeF size(img.width() * scale, img.height() * scale);
        const QImage* img = &this->img;
        for (int i = 0; i < speedy_images.size(); i++) {
            if (kmh >= speedy_images[i].kmh)
                img = &speedy_images[i].img;
        }
        painter.drawImage(QRectF(pos.x() - 0.5 * size.width(), pos.y() - size.height() + y_offset * scale, size.width(), size.height()), *img);
    }
    void add_speedy_image(QString path, const qreal kmh) {
        speedy_images.append(SpeedyImage{QImage(path), kmh});
    }

    QImage img;
    QVector<SpeedyImage> speedy_images;
    qreal scale;
    qreal y_offset;
};

struct Tree {
    Tree() {}
    Tree(const int tree_type, const qreal pos, const qreal scale = 1, const qreal speed_scale = 1)
        : type(tree_type), pos(pos), scale(scale), speed_scale(speed_scale) {}

    qreal track_x(const qreal cur_x) {
        return cur_x + (pos - cur_x) * speed_scale;
    }

    int type = 0; // which type of tree
    qreal pos; // position on the track
    qreal scale = 1; // size-multiplier
    qreal speed_scale = 1; // speed-multiplier
};

struct Track {
    struct SignImage {
        bool load(const QString filename, const QString name, const qreal scale) {
            is_svg = filename.endsWith("svg", Qt::CaseInsensitive);
            if (is_svg)
                svg = new QSvgRenderer(filename);
            else
                img = new QImage(filename);
            if (is_svg ? !svg->isValid() : img->isNull())
                return false;
            this->name = name;
            size = is_svg ? svg->defaultSize() : img->size();
            size *= scale;
            return true;
        }

        QImage* img = NULL;
        QSvgRenderer* svg = NULL;
        bool is_svg = false;
        QString name;
        QSizeF size;
    };

    struct Sign {
        enum Type {
            Stop = 0,
            Speed30,
            Speed40,
            Speed50,
            Speed60,
            Speed70,
            Speed80,
            Speed90,
            Speed100,
            Speed110,
            Speed120,
            Speed130,
            TrafficLight,
            __length
        };
        enum TrafficLightState {
            Red = 0,
            Red_pending,
            Yellow,
            Green,
            __State_length
        };
        struct TrafficLightInfo {
            qreal trigger_distance = 500; //200;
            std::pair<qreal,qreal> time_range = {7000,7000}; //{3000,3000}; //{0,3000};
        };
        Sign() {}
        Sign(const Type type, const qreal at_length) : type(type), at_length(at_length) {}
        bool operator<(const Sign& s2) const { return at_length < s2.at_length; }
        bool get_position(QRectF& pos, QRectF& pole_pos, QPainterPath& path) {
            if (at_length > path.length())
                return false;
            qreal const percent = path.percentAtLength(at_length);
            const QPointF p = path.pointAtPercent(percent);
            QSizeF& size = (type == TrafficLight ? images.traffic_light_images[traffic_light_state]
                                                 : images.sign_images[type]).size;
            pos = QRectF(p.x() - size.width()/2, p.y() - size.height() - images.pole_size.height(),
                          size.width(), size.height());
            pole_pos = QRectF(p.x() - 0.5 * images.pole_size.width(), p.y() - images.pole_size.height(), images.pole_size.width(), images.pole_size.height());
            return true;
        }
        bool draw(QPainter& painter, QPainterPath& path) {
            QRectF pos, pole_pos;
            if (!get_position(pos, pole_pos, path))
                return false;
            images.pole_image.render(&painter, pole_pos);
            Track::SignImage& img = type == TrafficLight ? images.traffic_light_images[traffic_light_state]
                                                 : images.sign_images[type];
            img.is_svg ? img.svg->render(&painter, pos) : painter.drawImage(pos, *img.img);
            return true;
        }
        bool is_speed_sign() const { return type >= Speed30 && type <= Speed130; }
        qreal speed_limit() const { Q_ASSERT(is_speed_sign()); return 30 + (type - Speed30) * 10; }

        Type type = Stop;
        qreal at_length = 0;
        TrafficLightState traffic_light_state = Red;
        TrafficLightInfo traffic_light_info;
    };

    void check_traffic_light_distance(Sign& s, qreal prev_pos = 0, const qreal min_dist = 100) {
        qreal& dist = s.traffic_light_info.trigger_distance;
        if (s.at_length - prev_pos - 50 < dist)
            dist = s.at_length - prev_pos - 50;
        if (dist < min_dist)
            dist = min_dist;
    }

    void prepare_track() {
        sort_signs();
        Sign* prev_traffic_light = NULL;
        for (Sign& s : signs) {
            if (s.type == Sign::TrafficLight) {
                check_traffic_light_distance(s, prev_traffic_light ? prev_traffic_light->at_length : 0);
                prev_traffic_light = &s;
            }
        }
    }

    void sort_signs() {
        std::sort(signs.begin(), signs.end());
    }

    QVector<QPointF> points;
    int num_points = 0;
    int width = 1000;
    QVector<Sign> signs;
    int max_time = 0; // how much time the user has to finish the track

    struct Images {
        QVector<SignImage> sign_images;
        QSvgRenderer pole_image;
        QSize pole_size;
        SignImage traffic_light_images[Sign::__State_length];
        void load_sign_images() {
            pole_image.load(QString("media/signs/pole.svg"));
            pole_size = pole_image.defaultSize() * 1;
            sign_images.resize(Sign::__length);
            sign_images[Sign::Stop].load("media/signs/stop-sign.svg", "Stop", 0.02);
            for (int i = 0; i < 11; i++) {
                QString path; path.sprintf("media/signs/%isign.svg", 30+i*10);
                QString name; name.sprintf("Speed: %i", 30+i*10);
                sign_images[Sign::Speed30 + i].load(path, name, 0.05);
            }
            traffic_light_images[Sign::Red].load("media/signs/traffic_red.svg", "", 0.07);
            traffic_light_images[Sign::Red_pending].load("media/signs/traffic_red.svg", "", 0.07);
            traffic_light_images[Sign::Yellow].load("media/signs/traffic_yellow.svg", "", 0.07);
            traffic_light_images[Sign::Green].load("media/signs/traffic_green.svg", "", 0.07);
            sign_images[Sign::TrafficLight].name = "Traffic Light";
        }
    };
    static Images images;

    inline bool save(const QString filename = QDir::homePath()+"/track.bin");
    inline bool load(const QString filename = QDir::homePath()+"/track.bin");
    void get_path(QPainterPath& path, const qreal height) {
        if (!points.size())
            return;
        const qreal h = height;
        path.moveTo(tf(points[0],h));
        for (int i = 0; i < num_points; i++) {
            const int j = i*3+1;
            path.cubicTo(tf(points[j],h), tf(points[j+1],h), tf(points[j+2],h));
        }
    }
    void get_path_points(QPainterPath& path, const qreal height) { // const bool main_points_only = false
        if (!points.size())
            return;
        const qreal h = height;
        path.moveTo(tf(points[0],h));
        for (int i = 1; i <= num_points*3; i++) {
//            if (main_points_only)
//                i += 2;
            path.lineTo(tf(points[i],h));
        }
    }

    //transform
    static inline QPointF tf(const QPointF& p, const qreal height) {
        return QPointF(p.x(), height - p.y());
    }
    // inverse transform
    static inline QPointF tf_1(const QPointF& p, const qreal height) {
        return tf(p, height);
    }
};

inline QDataStream &operator<<(QDataStream &out, const Track &track) {
    out << track.points << track.num_points << track.width << track.signs;
    return out;
}

inline QDataStream &operator>>(QDataStream &in, Track &track) {
    in >> track.points >> track.num_points >> track.width >> track.signs;
    return in;
}

inline QDataStream &operator<<(QDataStream &out, const Track::Sign &sign) {
    out << (int) sign.type << sign.at_length;
    return out;
}

inline QDataStream &operator>>(QDataStream &in, Track::Sign &sign) {
    int type;
    in >> type >> sign.at_length;
    sign.type = (Track::Sign::Type) type;
    return in;
}


bool Track::save(const QString filename) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream out(&file);
    out << *this;
    return true;
}
bool Track::load(const QString filename) {
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QDataStream in(&file);
    in >> *this;
    prepare_track();
    return true;
}

#endif // TRACK_H
