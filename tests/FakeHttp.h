#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <functional>

// A fake OpenAI-compatible HTTP server on localhost: tests never use the real network (issue #39).
class FakeHttp {
public:
    struct Req {
        QString method, path;
        QMap<QString, QByteArray> headers; // lower-case names
        QByteArray body;
    };
    struct Res {
        int status = 200;
        QByteArray body;
        bool hang = false;       // never answer
        QByteArray extraHeaders; // raw lines ending in CRLF
    };

    FakeHttp()
    {
        server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (QTcpSocket *s = server.nextPendingConnection())
                QObject::connect(s, &QTcpSocket::readyRead, s, [this, s] { handle(s); });
        });
    }
    QString url() const { return QStringLiteral("http://127.0.0.1:%1/v1").arg(server.serverPort()); }

    QTcpServer server;
    std::function<Res(const Req &)> handler;
    QList<Req> requests;

private:
    void handle(QTcpSocket *s)
    {
        QByteArray buf = s->property("buf").toByteArray() + s->readAll();
        s->setProperty("buf", buf);
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> lines = buf.left(end).split('\n');
        Req r;
        const QList<QByteArray> first = lines.first().trimmed().split(' ');
        r.method = QString::fromLatin1(first.value(0));
        r.path = QString::fromLatin1(first.value(1));
        int length = 0;
        for (int i = 1; i < lines.size(); ++i) {
            const int c = lines.at(i).indexOf(':');
            if (c < 0)
                continue;
            const QString name = QString::fromLatin1(lines.at(i).left(c).trimmed()).toLower();
            r.headers.insert(name, lines.at(i).mid(c + 1).trimmed());
            if (name == QLatin1String("content-length"))
                length = lines.at(i).mid(c + 1).trimmed().toInt();
        }
        if (buf.size() < end + 4 + length)
            return;
        r.body = buf.mid(end + 4, length);
        s->setProperty("buf", QByteArray());
        requests.append(r);
        const Res res = handler ? handler(r) : Res{404, "{}"};
        if (res.hang)
            return;
        s->write("HTTP/1.1 " + QByteArray::number(res.status) + " X\r\nContent-Type: application/json\r\n"
                 + res.extraHeaders + "Content-Length: " + QByteArray::number(res.body.size())
                 + "\r\nConnection: close\r\n\r\n" + res.body);
        s->disconnectFromHost();
    }
};
