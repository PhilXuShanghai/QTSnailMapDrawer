#ifndef MAPMATH_H
#define MAPMATH_H

// mapmath.h
#pragma once
#include <QPointF>
#include <QLineF>
#include <QtGlobal>
#include <cmath>
#include <QList>
#include <MapLineItem.h>
#include <MapPointItem.h>

namespace MapMath {

    // 该工具函数用于处理点吸附问题，是否要进行吸附
    // P 在线段 AB 上的比例参数，已 clamp 到 [0,1]
    inline qreal projectionT(const QPointF &p, const QPointF &a, const QPointF &b)
    {
        const QPointF v = b - a;
        const qreal len2 = v.x() * v.x() + v.y() * v.y();
        if (len2 <= 1e-12)          // A、B 重合，退化成点
            return 0.0;
        const QPointF w = p - a;
        const qreal t = (w.x() * v.x() + w.y() * v.y()) / len2;
        return qBound(0.0, t, 1.0);
    }

    // P 到线段 AB 的最近点
    inline QPointF closestPointOnSegment(const QPointF &p, const QPointF &a, const QPointF &b)
    {
        return QLineF(a, b).pointAt(projectionT(p, a, b));
    }

    // P 到线段 AB 的最近距离（吸附判定的核心）
    inline qreal distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
    {
        return QLineF(p, closestPointOnSegment(p, a, b)).length();
    }

    inline void fitLineTLS(const QList<QPointF> &pts, QPointF &centroid, QPointF &dir)
    {
        centroid = QPointF(0, 0);
        if (pts.isEmpty()) {
            dir = QPointF(1, 0);
            return;
        }
        for (const QPointF &p : pts)
            centroid += p;
        centroid /= qreal(pts.size());

        qreal sxx = 0.0, syy = 0.0, sxy = 0.0;
        for (const QPointF &p : pts) {
            const qreal dx = p.x() - centroid.x();
            const qreal dy = p.y() - centroid.y();
            sxx += dx * dx;
            syy += dy * dy;
            sxy += dx * dy;
        }
        // 协方差矩阵主轴方向
        const qreal theta = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
        dir = QPointF(std::cos(theta), std::sin(theta));
    }

    // 点在直线上的投影
    inline QPointF projectOnLine(const QPointF &p, const QPointF &a, const QPointF &dir)
    {
        const QPointF w = p - a;
        const qreal t = w.x() * dir.x() + w.y() * dir.y();
        return a + t * dir;
    }

    // 把角度归一化到 [0, pi)
    inline qreal normalizeAngleHalfPi(qreal ang)
    {
        const qreal pi = std::acos(-1.0);
        while (ang < 0.0)  ang += pi;
        while (ang >= pi)  ang -= pi;
        return ang;
    }

    // 辅助：两条共享端点的线之间的最小夹角（0~π/2，线无向）
    inline qreal  angleBetween (MapLineItem *l1, MapLineItem *l2, MapPointItem *shared)
    {
        QPointF d1 = (l1->startPoint() == shared)
        ? (l1->endPoint()->pos() - shared->pos())
        : (l1->startPoint()->pos() - shared->pos());
        QPointF d2 = (l2->startPoint() == shared)
                         ? (l2->endPoint()->pos() - shared->pos())
                         : (l2->startPoint()->pos() - shared->pos());
        qreal len1 = std::hypot(d1.x(), d1.y());
        qreal len2 = std::hypot(d2.x(), d2.y());
        if (len1 < 1e-9 || len2 < 1e-9) return 0.0;
        qreal dot = (d1.x()*d2.x() + d1.y()*d2.y()) / (len1 * len2);
        dot = qBound(-1.0, dot, 1.0);
        return std::acos(std::abs(dot));
    }
}// namespace MapMath
#endif // MAPMATH_H
