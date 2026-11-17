/* SPDX-License-Identifier: GPL-3.0-or-later
 * Read the generated .pvs using Qt and GVariant, just as PulseView does.
 * In particular, Python integer decoder options must be signed int64 (x),
 * and the embedded NUL bytes must survive QSettings' QByteArray parsing. */
#include <QCoreApplication>
#include <QSettings>
#include <QByteArray>
#include <glib.h>
#include <cstdio>

static bool option(QSettings &s, const QString &path, const char *type, gint64 number)
{
    s.beginGroup(path);
    QByteArray bytes = s.value("value").toByteArray();
    bool ok = s.value("type").toString() == type && bytes.size() == 8;
    if (ok) {
        GVariant *v = g_variant_new_from_data(G_VARIANT_TYPE_INT64, bytes.constData(),
                                              bytes.size(), false, nullptr, nullptr);
        ok = g_variant_get_int64(v) == number;
        g_variant_unref(v);
    }
    s.endGroup();
    return ok;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    QSettings s(argv[1], QSettings::IniFormat);
    bool ok = s.value("decode_signals").toInt() == 4;
    for (int i = 0; i < 8; i++) ok &= s.value(QString("CH%1/enabled").arg(i)).toBool();
    ok &= option(s, "decode_signal0/decoder0/option0", "x", 115200);
    ok &= option(s, "decode_signal3/decoder0/option0", "x", 250000);
    ok &= s.value("decode_signal0/decoder0/option1/value").toByteArray() == QByteArray("hex\0", 4);
    const char *ids[] = {"uart", "i2c", "spi", "can"};
    const int mapping[][4] = {{0, -1, -1, -1}, {1, 2, -1, -1}, {4, 6, 5, 3}, {7, -1, -1, -1}};
    const int count[] = {2, 2, 4, 1};
    for (int d = 0; d < 4; d++) {
        s.beginGroup(QString("decode_signal%1").arg(d));
        ok &= s.value("decoder0/id").toString() == ids[d];
        ok &= s.value("channels").toInt() == count[d];
        for (int c = 0; c < count[d]; c++) {
            QString key = QString("channel%1/assigned_signal_name").arg(c);
            if (mapping[d][c] >= 0) ok &= s.value(key).toString() == QString("CH%1").arg(mapping[d][c]);
            else ok &= !s.contains(key);
        }
        s.endGroup();
    }
    if (s.contains("rp2350/samplerate")) {
        ok &= s.value("rp2350/samplerate").toULongLong() == 25000000;
        ok &= s.value("rp2350/samples").toInt() == 65536;
    }
    std::puts(ok ? "PASS native Qt/GVariant setup" : "FAIL native Qt/GVariant setup");
    return ok ? 0 : 1;
}
