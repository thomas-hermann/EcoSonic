#ifndef ENGINE_H
#define ENGINE_H

#include "consumption_map.h"
#include "torque_map.h"
#include <math.h>
#include "OSCSender.h"

struct ConsumptionMonitor
{
    // liters_s [L/s], t [s]
    inline void tick(double liters_s, double dt, double speed) {
        liters_used += dt * liters_s;
        t_counter += dt;
        liter_counter += dt * liters_s;
        liters_per_second_cont = liters_s;
        liters_per_100km_cont = liters_s / speed * 1000 * 100;
        ml_counter += dt * liters_s;
        if (ml_counter >= ml_per_tick * 0.001) {
            ml_counter -= ml_per_tick * 0.001;
            osc->send_float("/consumption_tick", liters_per_100km_cont);
        }
    }
    inline double l_100km_instantaneous(const double liter_s, const double speed) {
        return liter_s / speed * 1000 * 100;
    }

    // returns true if avg makes for > 1 sek
    // l_100km [L/100km], speed [m/s]
    inline bool get_l_100km(double& l_100km, const double speed) {
        if (t_counter >= 1) {
            const double L_s = liter_counter / t_counter;
            l_100km = L_s / speed * 1000 * 100; // [L/s] => [L/100km]
            liter_counter = t_counter = 0; // reset counter
            return true;
        } else
            return false;
    }
    inline void reset() {
        liters_used = 0;
        liter_counter = 0;
        t_counter = 0;
    }
    double ml_per_tick = 2;
    OSCSender* osc = NULL;
    double ml_counter = 0; // counter for tick-production
    double liters_used = 0; // total usage
    double liter_counter = 0; // counter for calculation of avg l/100km
    double t_counter = 0;
    double liters_per_second_cont = 0;
    double liters_per_100km_cont = 0;
};

class Engine
{
public:

    Engine(const qreal max_rpm, const qreal max_torque);

    void reset() {
        //set_rpm(2500);
        set_rpm(700);
    }

    // throttle (0..1)
    inline qreal update_torque(qreal throttle)
    {
        if (throttle < min_throttle && rpm() < 700) // ansonsten: Schubabschaltung!
            throttle = min_throttle;
        torque = max_torque * torque_map.get_torque(throttle, rel_rpm());
        //Q_ASSERT(torque != 0);
        torque_out = torque - engine_braking_coefficient * pow(std::max(rpm(), 0.) / 60, 1.1)
                            - engine_braking_offset;
        //printf("%.3f %.3f\n", torque, torque_out);
        Q_ASSERT(!isnan(torque_out));
        Q_ASSERT(torque_out <= torque);
        return torque_out;
    }

    // returns [kW]
    inline qreal power_output() const {
        return angular_velocity * torque / 1000;
    }

    // returns [g/h] (?)
    inline qreal get_consumption() const {
        qreal const power = power_output();
        qreal rel_consumption = consumption_map.get_rel_consumption(rel_rpm(), torque / max_torque);
        return rel_consumption * base_consumption * power;
    }

    // returns [L/s]
    inline qreal get_consumption_L_s() {
        return get_consumption() / 1000 / 0.75 / (60*60); // [g/h] => [L/s]
    }

    // consumption [g/h], speed [m/s]
    inline qreal get_l100km(qreal speed, qreal consumption = -1) {
        if (speed < 0.001)
            return 0;
        if (consumption == -1)
            consumption = get_consumption();
        consumption /= 60*60; // [g/s]
        consumption /= speed * 1000; // [kg/m]
        consumption /= 0.75; // /dichte (0.75 kg/L) => [L/m]
        return consumption * 100 * 1000; // [L/100km]
    }

    inline double rpm() const { return angular_velocity * 60 / (2*M_PI); }
    inline void set_rpm(qreal rpm) {
        Q_ASSERT(!isnan(rpm));
        angular_velocity = rpm2angular_velocity(rpm);
    }
    inline double rel_rpm() const { return rpm() / max_rpm; }
    static inline double rpm2angular_velocity(qreal const rpm) { return rpm * 2 * M_PI / 60; }
    static inline double angular_velocity2rpm(qreal const av) { return av * 30 / M_PI; }

    ConsumptionMap consumption_map;
    qreal base_consumption = 100; // 206;
    TorqueMap torque_map;
    qreal max_rpm; // [u/min] // const
    qreal max_torque; // [N*m] // const
    qreal engine_braking_coefficient = 0.9; // const
    qreal engine_braking_offset = 0; // [N*m] // const
    qreal angular_velocity = 0; // [rad/s]
    //qreal rpm; // current rpm [u/min]
    qreal torque; // [N*m]
    qreal torque_out; // [N*m]
    qreal torque_counter; // [N*m] "counter force"
    qreal min_throttle = 0.07; // min throttle
    qreal inertia = 0.13; // [kg m^2]
};

inline QDataStream &operator<<(QDataStream &out, const Engine &engine)
{
    out << engine.consumption_map.ellipse << engine.consumption_map.transform << engine.base_consumption << engine.torque_map.torque_ramps << engine.max_rpm <<
           engine.max_torque << engine.engine_braking_coefficient << engine.engine_braking_offset << engine.min_throttle << engine.inertia;
    return out;
}

inline QDataStream &operator>>(QDataStream &in, Engine &engine) {
    in >> engine.consumption_map.ellipse >> engine.consumption_map.transform >> engine.base_consumption >> engine.torque_map.torque_ramps >> engine.max_rpm >>
           engine.max_torque >> engine.engine_braking_coefficient >> engine.engine_braking_offset >> engine.min_throttle >> engine.inertia;
    return in;
}


#endif // ENGINE_H
