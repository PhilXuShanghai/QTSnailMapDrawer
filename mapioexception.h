#ifndef MapIOException_H
#define MapIOException_H
#include <QException>
#include <QString>

class MapIOException : public QException
{
public:
    explicit MapIOException(const QString &msg) : m_msg(msg) {}

    // QException 要求实现 clone / raise
    void raise() const override { throw *this; }
    MapIOException *clone() const override { return new MapIOException(*this); }

    QString message() const { return m_msg; }

private:
    QString m_msg;
};
#endif