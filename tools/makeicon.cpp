// Developer tool: renders an SVG into a multi-resolution .ico (PNG-compressed entries).
// Usage: tessera_makeicon <in.svg> <out.ico>
#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSaveFile>
#include <QSvgRenderer>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    if (argc < 3)
        return 2;
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid())
        return 1;
    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> pngs;
    for (int s : sizes) {
        QImage img(s, s, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        svg.render(&p);
        p.end();
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");
        pngs << data;
    }
    QByteArray out;
    auto le16 = [&out](quint16 v) { out.append(char(v & 0xff)).append(char(v >> 8)); };
    auto le32 = [&out](quint32 v) { for (int i = 0; i < 4; ++i) out.append(char((v >> (8 * i)) & 0xff)); };
    le16(0); le16(1); le16(quint16(pngs.size()));
    quint32 offset = 6 + 16 * pngs.size();
    for (int i = 0; i < pngs.size(); ++i) {
        const int s = sizes[i];
        out.append(char(s == 256 ? 0 : s)).append(char(s == 256 ? 0 : s)).append(char(0)).append(char(0));
        le16(1); le16(32); le32(quint32(pngs[i].size())); le32(offset);
        offset += quint32(pngs[i].size());
    }
    for (const QByteArray &p : pngs)
        out.append(p);
    QSaveFile f(QString::fromLocal8Bit(argv[2]));
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit())
        return 1;
    return 0;
}