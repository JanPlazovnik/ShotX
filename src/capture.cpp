#include "capture.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusArgument>
#include <QDBusUnixFileDescriptor>
#include <QGuiApplication>
#include <QScreen>

#include <cerrno>
#include <fcntl.h>
#include <memory>
#include <thread>
#include <unistd.h>

namespace {

struct Request {
    QDBusPendingCall call{QDBusPendingCall::fromCompletedCall(QDBusMessage())};
    int readFd = -1;
    QByteArray data;
    std::thread reader;
};

// KWin writes the pixels into the pipe while the reply is in flight, so the read end is drained on a
// separate thread until KWin closes its copy of the write end.
std::unique_ptr<Request> start(const QString &method, QVariantList args)
{
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) {
        return nullptr;
    }

    auto request = std::make_unique<Request>();
    request->readFd = fds[0];
    request->reader = std::thread([r = request.get()] {
        char buffer[1 << 16];
        for (;;) {
            const ssize_t n = read(r->readFd, buffer, sizeof buffer);
            if (n > 0) {
                r->data.append(buffer, n);
            } else if (n == 0 || errno != EINTR) {
                break;
            }
        }
    });

    const QVariantMap options{
        {QStringLiteral("include-cursor"), false},
        {QStringLiteral("include-decoration"), true},
        {QStringLiteral("include-shadow"), false},
        {QStringLiteral("native-resolution"), true},
    };
    args << options << QVariant::fromValue(QDBusUnixFileDescriptor(fds[1]));
    close(fds[1]);

    QDBusMessage message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"),
                                                          QStringLiteral("/org/kde/KWin/ScreenShot2"),
                                                          QStringLiteral("org.kde.KWin.ScreenShot2"),
                                                          method);
    message.setArguments(args);
    request->call = QDBusConnection::sessionBus().asyncCall(message);
    return request;
}

QImage finish(Request &request, QString *error)
{
    request.call.waitForFinished();
    const QDBusMessage reply = request.call.reply();
    // The pending call keeps the sent message, and with it a duplicate of the pipe's write end, alive.
    // Dropping it lets the reader see EOF once KWin closes its copy.
    request.call = QDBusPendingCall::fromCompletedCall(QDBusMessage());
    request.reader.join();
    close(request.readFd);

    if (reply.type() == QDBusMessage::ErrorMessage) {
        *error = reply.errorMessage();
        return {};
    }

    const QVariantMap result = qdbus_cast<QVariantMap>(reply.arguments().value(0));
    const int width = result.value(QStringLiteral("width")).toInt();
    const int height = result.value(QStringLiteral("height")).toInt();
    const qsizetype stride = result.value(QStringLiteral("stride")).toLongLong();
    const auto format = QImage::Format(result.value(QStringLiteral("format")).toUInt());
    if (result.value(QStringLiteral("type")).toString() != QLatin1String("raw") || width <= 0 || height <= 0
        || request.data.size() < stride * height) {
        *error = QStringLiteral("KWin returned an unexpected screenshot");
        return {};
    }

    auto *buffer = new QByteArray(std::move(request.data));
    return QImage(reinterpret_cast<uchar *>(buffer->data()), width, height, stride, format,
                  [](void *data) { delete static_cast<QByteArray *>(data); }, buffer);
}

QImage captureOne(const QString &method, QString *error)
{
    auto request = start(method, {});
    if (!request) {
        *error = QStringLiteral("Could not create a pipe");
        return {};
    }
    return finish(*request, error);
}

}

QList<ScreenShot> Capture::allScreens(QString *error)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    std::vector<std::unique_ptr<Request>> requests;
    for (QScreen *screen : screens) {
        requests.push_back(start(QStringLiteral("CaptureScreen"), {screen->name()}));
        if (!requests.back()) {
            *error = QStringLiteral("Could not create a pipe");
        }
    }

    QList<ScreenShot> shots;
    for (size_t i = 0; i < requests.size(); ++i) {
        if (!requests[i]) {
            continue;
        }
        QImage image = finish(*requests[i], error);
        if (!image.isNull()) {
            shots.append({screens[i], image});
        }
    }
    return shots;
}

QImage Capture::activeScreen(QString *error)
{
    return captureOne(QStringLiteral("CaptureActiveScreen"), error);
}

QImage Capture::activeWindow(QString *error)
{
    return captureOne(QStringLiteral("CaptureActiveWindow"), error);
}
