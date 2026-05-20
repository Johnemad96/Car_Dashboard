#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "dashboard_backend.h"
#include "rpicameraimageprovider.h"
#include <csignal>

static void signalHandler(int) {
    QCoreApplication::quit();
}

int main(int argc, char *argv[])
{

    QGuiApplication app(argc, argv);
    // signal(SIGINT, signalHandler);
    // signal(SIGTERM, signalHandler);

    QQmlApplicationEngine engine;

    rclcpp::init(argc, argv);

    DashboardBackend dashboard;

    // THIS IS THE BRIDGE.
    // Injects the C++ object into the QML engine under the name "backend".
    engine.rootContext()->setContextProperty("dashboard", &dashboard);
    engine.addImageProvider(QStringLiteral("rpicamera"), new RpiCameraImageProvider(&dashboard));

    const QUrl url(QStringLiteral("qrc:/qtdashboard/Main.qml"));
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.load(url);

    return app.exec();
}
