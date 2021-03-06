#ifndef HUD_H
#define HUD_H

#include <type_traits>
#include <QTextStream>
#include <QTime>
#include <QPainter>
#include <math.h>
#include <array>

struct Speedometer {
	void draw(QPainter& painter, const QPointF pos, const qreal kmh);

    qreal max_speed() { return min_speed + speed_steps * speed_delta; }

    qreal radius = 100;
    qreal gap = M_PI/2; // gap at the bottom [degree]
    int min_speed = 10;
    int speed_delta = 20;
    int speed_steps = 10; // how many steps between min and max speed
    QString caption = "km/h";
};

struct RevCounter : public Speedometer
{
    RevCounter() {
        min_speed = 0;
        speed_delta = 10;
        speed_steps = 7;
        caption = "tr/min x100";
    }
};

struct ConsumptionDisplay {
    ConsumptionDisplay(QString legend1 = "L/", QString legend2 = "100km", const char* const number_format = "%04.1f")
        : number_format(number_format)
    {
		fonts[0] = { "Eurostile", 18, QFont::Bold };
		fonts[1] = { "Eurostile", 12, QFont::Bold };
		fonts[2] = { "Eurostile", 8, QFont::Bold };
        legend[0] = legend1;
        legend[1] = legend2;
        for (int i = 0; i < 3; i++) {
			font_metrics.emplace_back(fonts[i]);
            font_heights[i] = font_metrics[i].height();
        }
    }
	void draw(QPainter& painter, QPointF pos, qreal consumption, bool draw_number = true);

    // fonts for number, 'L/', 100kmh
	QFont fonts[3]; //{ { "Eurostile", 18, QFont::Bold }, { "Eurostile", 12, QFont::Bold }, { "Eurostile", 8, QFont::Bold } };
	std::vector<QFontMetrics> font_metrics; // = { QFontMetrics(fonts[0]), QFontMetrics(fonts[1]), QFontMetrics(fonts[2]) };
    //	static const std::array<QFont, 3> fonts;
    //	static const QFontMetrics font_metrics[3];
    qreal font_heights[3];
	std::array<QString, 2> legend = { { "L/", "100km" } };
    const char* const number_format;
};


struct TimeDisplay {
    static void draw(QPainter& painter, QPointF pos, int max_time, qreal time_elapsed, bool track_started, bool draw_remaining)
    {
        QTime t(0,0);
        if (draw_remaining)
            t = t.addSecs(std::max(0.0, track_started ? max_time - time_elapsed : max_time));
        else if (track_started)
            t = t.addSecs(time_elapsed);
        QString ts = t.toString("m:ss");
        painter.setFont(QFont{"Eurostile", 18, QFont::Bold});
        painter.setPen(QPen(Qt::black));
        painter.drawText(pos, (draw_remaining ? "Remaining Time: " : "Elapsed Time: ") + ts);
    }
};

struct HUD {
    ConsumptionDisplay consumption_display;
    ConsumptionDisplay trip_consumption{"dl/", "Trip", "%04.2f"};
    Speedometer speedometer;
    RevCounter rev_counter;
    TimeDisplay time_display;
    qreal l_100km = 0;
    bool draw_consumption = true;
    void draw(QPainter& painter, const qreal width, const qreal rpm, const qreal kmh, const qreal liters_used) { //, int max_time, qreal time_elapsed, bool track_started) {
        const qreal gap = 15;
        const qreal mid = width / 2.;
        rev_counter.draw(painter, QPointF(mid - rev_counter.radius - gap, rev_counter.radius + gap), 0.01 * rpm);
        speedometer.draw(painter, QPointF(mid + speedometer.radius + gap, speedometer.radius + gap), kmh);
        consumption_display.draw(painter, QPointF(mid, 180), l_100km, kmh >= 5);
        if (draw_consumption) {
            trip_consumption.draw(painter, QPointF(mid, 25), liters_used * 10);
        }
    }
    const qreal height = 230;
};

#endif
