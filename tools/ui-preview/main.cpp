// Desktop preview of the UI kit: every component, and the Home page on the
// three device profiles, at true pixel size (1 image pixel = 1 panel pixel).
//
//   smalt_ui_preview                      show a window (needs a display)
//   smalt_ui_preview --device libra       one device: clara-colour, clara-bw, libra, or all
//   smalt_ui_preview --out DIR            write PNGs instead of showing a window,
//                                         works headless with QT_QPA_PLATFORM=offscreen
//   smalt_ui_preview --font FAMILY        use another font family (default: generic sans)
//
// Compare the PNGs with docs/mockups/. Files are named <device>-frame.png and
// <device>-<component>.png.
#include "kit/gallery.h"

#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QScrollArea>
#include <QStringList>
#include <QTextStream>
#include <QVBoxLayout>

using namespace smalt;
using namespace smalt::ui;

namespace {

struct Device
{
    const char *key;
    DeviceModel model;
};

const Device kDevices[] = {
    { "clara-colour", DeviceModel::ClaraColour },
    { "clara-bw", DeviceModel::ClaraBW },
    { "libra", DeviceModel::LibraColour },
};

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString deviceArg = QStringLiteral("all");
    QString outDir;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--device") && i + 1 < args.size())
            deviceArg = args.at(++i);
        else if (args.at(i) == QLatin1String("--out") && i + 1 < args.size())
            outDir = args.at(++i);
        else if (args.at(i) == QLatin1String("--font") && i + 1 < args.size())
            kit::setFontFamily(args.at(++i));
        else {
            QTextStream(stderr)
                    << "usage: smalt_ui_preview [--device NAME|all] [--out DIR] [--font FAMILY]\n";
            return 2;
        }
    }

    QList<QPair<QString, QImage>> images;
    bool matched = false;
    for (const Device &d : kDevices) {
        if (deviceArg != QLatin1String("all") && deviceArg != QLatin1String(d.key))
            continue;
        matched = true;
        kit::KitFactory factory(d.model, nullptr);
        QWidget *frame = kit::gallery::buildHomeFrame(factory);
        images.append(qMakePair(QStringLiteral("%1-frame").arg(QLatin1String(d.key)),
                                kit::gallery::renderToImage(frame)));
        delete frame;
        const QStringList names = kit::gallery::sampleNames();
        for (int i = 0; i < names.size(); ++i) {
            QWidget *sample =
                    kit::gallery::buildSample(names.at(i), factory, factory.profile().widthPx);
            images.append(qMakePair(QStringLiteral("%1-%2").arg(QLatin1String(d.key), names.at(i)),
                                    kit::gallery::renderToImage(sample)));
            delete sample;
        }
    }
    if (!matched) {
        QTextStream(stderr) << "unknown device: " << deviceArg << "\n";
        return 2;
    }

    if (!outDir.isEmpty()) {
        if (!QDir().mkpath(outDir)) {
            QTextStream(stderr) << "cannot create " << outDir << "\n";
            return 1;
        }
        for (int i = 0; i < images.size(); ++i) {
            const QString path = QDir(outDir).filePath(images.at(i).first + QStringLiteral(".png"));
            if (!images.at(i).second.save(path)) {
                QTextStream(stderr) << "cannot write " << path << "\n";
                return 1;
            }
        }
        QTextStream(stdout) << "wrote " << images.size() << " images to " << outDir << "\n";
        return 0;
    }

    QScrollArea scroll;
    QWidget *content = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(content);
    for (int i = 0; i < images.size(); ++i) {
        QLabel *caption = new QLabel(images.at(i).first);
        QLabel *picture = new QLabel;
        picture->setPixmap(QPixmap::fromImage(images.at(i).second));
        picture->setFrameStyle(QFrame::Box);
        layout->addWidget(caption);
        layout->addWidget(picture, 0, Qt::AlignLeft);
    }
    scroll.setWidget(content);
    scroll.resize(1200, 900);
    scroll.show();
    return app.exec();
}
