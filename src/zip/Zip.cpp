#include "zip/Zip.h"

#include <QDateTime>
#include <QtEndian>

#include <array>

namespace zip {

// ---------------------------------------------------------------------------
// CRC32
// ---------------------------------------------------------------------------

static const std::array<quint32, 256> &crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[n] = c;
        }
        return t;
    }();
    return table;
}

quint32 crc32(const QByteArray &data)
{
    const auto &t = crcTable();
    quint32 c = 0xFFFFFFFFu;
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    for (qsizetype i = 0; i < data.size(); ++i)
        c = t[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------------------------------------------------------------------
// Raw DEFLATE decoder (RFC 1951)
// ---------------------------------------------------------------------------

namespace {

constexpr int MaxBits = 15;
constexpr int MaxLCodes = 286;
constexpr int MaxDCodes = 30;
constexpr int FixLCodes = 288;

struct Huffman {
    short count[MaxBits + 1];
    short symbol[FixLCodes];
};

struct Inflater {
    const uchar *in = nullptr;
    qsizetype inLen = 0;
    qsizetype inPos = 0;
    quint32 bitBuf = 0;
    int bitCnt = 0;
    QByteArray out;
    bool err = false;

    int bits(int need)
    {
        quint32 val = bitBuf;
        while (bitCnt < need) {
            if (inPos >= inLen) {
                err = true;
                return 0;
            }
            val |= quint32(in[inPos++]) << bitCnt;
            bitCnt += 8;
        }
        bitBuf = val >> need;
        bitCnt -= need;
        return int(val & ((1u << need) - 1));
    }

    bool stored()
    {
        bitBuf = 0;
        bitCnt = 0;
        if (inPos + 4 > inLen)
            return false;
        unsigned len = in[inPos] | (in[inPos + 1] << 8);
        unsigned nlen = in[inPos + 2] | (in[inPos + 3] << 8);
        inPos += 4;
        if (len != (~nlen & 0xFFFF))
            return false;
        if (inPos + qsizetype(len) > inLen)
            return false;
        out.append(reinterpret_cast<const char *>(in + inPos), qsizetype(len));
        inPos += len;
        return true;
    }

    int decode(const Huffman &h)
    {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= MaxBits; ++len) {
            code |= bits(1);
            if (err)
                return -1;
            int count = h.count[len];
            if (code - count < first)
                return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }

    static int construct(Huffman &h, const short *length, int n)
    {
        for (int len = 0; len <= MaxBits; ++len)
            h.count[len] = 0;
        for (int sym = 0; sym < n; ++sym)
            h.count[length[sym]]++;
        if (h.count[0] == n)
            return 0;
        int left = 1;
        for (int len = 1; len <= MaxBits; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0)
                return left;
        }
        short offs[MaxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < MaxBits; ++len)
            offs[len + 1] = offs[len] + h.count[len];
        for (int sym = 0; sym < n; ++sym)
            if (length[sym] != 0)
                h.symbol[offs[length[sym]]++] = short(sym);
        return left;
    }

    bool codes(const Huffman &lencode, const Huffman &distcode)
    {
        static const short lbase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                        31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                       2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dbase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                        193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                        6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                       6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int sym = decode(lencode);
            if (sym < 0 || err)
                return false;
            if (sym < 256) {
                out.append(char(sym));
            } else if (sym == 256) {
                return true;
            } else {
                sym -= 257;
                if (sym >= 29)
                    return false;
                int len = lbase[sym] + bits(lext[sym]);
                int dsym = decode(distcode);
                if (dsym < 0 || dsym >= 30 || err)
                    return false;
                qsizetype dist = dbase[dsym] + bits(dext[dsym]);
                if (err || dist > out.size())
                    return false;
                qsizetype from = out.size() - dist;
                for (int k = 0; k < len; ++k)
                    out.append(out.at(from + k));
            }
        }
    }

    bool fixed()
    {
        static Huffman lencode, distcode;
        static bool built = false;
        if (!built) {
            short lengths[FixLCodes];
            int sym = 0;
            for (; sym < 144; ++sym) lengths[sym] = 8;
            for (; sym < 256; ++sym) lengths[sym] = 9;
            for (; sym < 280; ++sym) lengths[sym] = 7;
            for (; sym < FixLCodes; ++sym) lengths[sym] = 8;
            construct(lencode, lengths, FixLCodes);
            for (sym = 0; sym < MaxDCodes; ++sym) lengths[sym] = 5;
            construct(distcode, lengths, MaxDCodes);
            built = true;
        }
        return codes(lencode, distcode);
    }

    bool dynamic()
    {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        short lengths[MaxLCodes + MaxDCodes];
        int nlen = bits(5) + 257;
        int ndist = bits(5) + 1;
        int ncode = bits(4) + 4;
        if (err || nlen > MaxLCodes || ndist > MaxDCodes)
            return false;
        int index = 0;
        for (; index < ncode; ++index)
            lengths[order[index]] = short(bits(3));
        for (; index < 19; ++index)
            lengths[order[index]] = 0;
        if (err)
            return false;
        Huffman lencode, distcode;
        if (construct(lencode, lengths, 19) != 0)
            return false;
        index = 0;
        while (index < nlen + ndist) {
            int sym = decode(lencode);
            if (sym < 0 || err)
                return false;
            if (sym < 16) {
                lengths[index++] = short(sym);
            } else {
                short len = 0;
                int rep;
                if (sym == 16) {
                    if (index == 0)
                        return false;
                    len = lengths[index - 1];
                    rep = 3 + bits(2);
                } else if (sym == 17) {
                    rep = 3 + bits(3);
                } else {
                    rep = 11 + bits(7);
                }
                if (err || index + rep > nlen + ndist)
                    return false;
                while (rep--)
                    lengths[index++] = len;
            }
        }
        if (lengths[256] == 0)
            return false;
        int e = construct(lencode, lengths, nlen);
        if (e < 0 || (e > 0 && nlen - lencode.count[0] != 1))
            return false;
        e = construct(distcode, lengths + nlen, ndist);
        if (e < 0 || (e > 0 && ndist - distcode.count[0] != 1))
            return false;
        return codes(lencode, distcode);
    }

    bool run()
    {
        int last;
        do {
            last = bits(1);
            int type = bits(2);
            if (err)
                return false;
            bool ok = false;
            switch (type) {
            case 0: ok = stored(); break;
            case 1: ok = fixed(); break;
            case 2: ok = dynamic(); break;
            default: ok = false; break;
            }
            if (!ok)
                return false;
        } while (!last);
        return true;
    }
};

} // namespace

QByteArray inflateRaw(const QByteArray &in, qsizetype expectedSize, bool *ok)
{
    Inflater inf;
    inf.in = reinterpret_cast<const uchar *>(in.constData());
    inf.inLen = in.size();
    if (expectedSize > 0)
        inf.out.reserve(expectedSize);
    bool good = inf.run();
    if (ok)
        *ok = good;
    return good ? inf.out : QByteArray();
}

QByteArray deflateRaw(const QByteArray &in)
{
    if (in.isEmpty())
        return QByteArray(); // caller stores empty entries uncompressed
    // qCompress: 4-byte big-endian length + zlib stream (2-byte header, raw deflate, 4-byte adler32)
    QByteArray z = qCompress(in, 6);
    if (z.size() < 4 + 2 + 4)
        return QByteArray();
    return z.mid(6, z.size() - 6 - 4);
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

static quint16 rd16(const QByteArray &b, qsizetype off)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + off));
}
static quint32 rd32(const QByteArray &b, qsizetype off)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + off));
}

bool Reader::open(const QByteArray &archive)
{
    m_buf = archive;
    m_entries.clear();
    m_order.clear();
    m_error.clear();

    const qsizetype n = m_buf.size();
    if (n < 22) {
        m_error = QStringLiteral("not a zip archive (too small)");
        return false;
    }
    qsizetype eocd = -1;
    const qsizetype stop = qMax<qsizetype>(0, n - 22 - 65535);
    for (qsizetype i = n - 22; i >= stop; --i) {
        if (rd32(m_buf, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        m_error = QStringLiteral("not a zip archive (no end of central directory)");
        return false;
    }
    const quint16 count = rd16(m_buf, eocd + 10);
    const quint32 cdOffset = rd32(m_buf, eocd + 16);
    qsizetype p = cdOffset;
    for (int i = 0; i < count; ++i) {
        if (p + 46 > n || rd32(m_buf, p) != 0x02014b50u) {
            m_error = QStringLiteral("corrupt central directory");
            return false;
        }
        Info info;
        const quint16 flags = rd16(m_buf, p + 8);
        info.method = rd16(m_buf, p + 10);
        info.crc = rd32(m_buf, p + 16);
        info.csize = rd32(m_buf, p + 20);
        info.usize = rd32(m_buf, p + 24);
        const quint16 nameLen = rd16(m_buf, p + 28);
        const quint16 extraLen = rd16(m_buf, p + 30);
        const quint16 commentLen = rd16(m_buf, p + 32);
        info.offset = rd32(m_buf, p + 42);
        if (p + 46 + nameLen > n) {
            m_error = QStringLiteral("corrupt central directory entry");
            return false;
        }
        const QByteArray rawName = m_buf.mid(p + 46, nameLen);
        const QString name = (flags & 0x0800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        if (!name.endsWith(QLatin1Char('/'))) {
            m_entries.insert(name, info);
            m_order.append(name);
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    return true;
}

QByteArray Reader::read(const QString &name, bool *ok) const
{
    if (ok)
        *ok = false;
    auto it = m_entries.constFind(name);
    if (it == m_entries.constEnd())
        return QByteArray();
    const Info &info = it.value();
    const qsizetype lh = info.offset;
    if (lh + 30 > m_buf.size() || rd32(m_buf, lh) != 0x04034b50u)
        return QByteArray();
    const quint16 nameLen = rd16(m_buf, lh + 26);
    const quint16 extraLen = rd16(m_buf, lh + 28);
    const qsizetype dataOff = lh + 30 + nameLen + extraLen;
    if (dataOff + qsizetype(info.csize) > m_buf.size())
        return QByteArray();
    const QByteArray comp = m_buf.mid(dataOff, info.csize);
    QByteArray out;
    if (info.method == 0) {
        out = comp;
    } else if (info.method == 8) {
        bool good = false;
        out = inflateRaw(comp, info.usize, &good);
        if (!good)
            return QByteArray();
    } else {
        return QByteArray();
    }
    if (crc32(out) != info.crc)
        return QByteArray();
    if (ok)
        *ok = true;
    return out;
}

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

static void put16(QByteArray &b, quint16 v)
{
    char c[2];
    qToLittleEndian<quint16>(v, c);
    b.append(c, 2);
}
static void put32(QByteArray &b, quint32 v)
{
    char c[4];
    qToLittleEndian<quint32>(v, c);
    b.append(c, 4);
}

QByteArray write(const QList<Entry> &entries)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    const quint16 dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    const quint16 dosDate = quint16(((qMax(1980, d.year()) - 1980) << 9) | (d.month() << 5) | d.day());

    QByteArray out;
    QByteArray central;
    for (const Entry &e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        QByteArray comp = deflateRaw(e.data);
        quint16 method = 8;
        if (comp.isEmpty() || comp.size() >= e.data.size()) {
            comp = e.data;
            method = 0;
        }
        const quint32 offset = quint32(out.size());

        put32(out, 0x04034b50u);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, method);
        put16(out, dosTime);
        put16(out, dosDate);
        put32(out, crc);
        put32(out, quint32(comp.size()));
        put32(out, quint32(e.data.size()));
        put16(out, quint16(name.size()));
        put16(out, 0);
        out.append(name);
        out.append(comp);

        put32(central, 0x02014b50u);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, method);
        put16(central, dosTime);
        put16(central, dosDate);
        put32(central, crc);
        put32(central, quint32(comp.size()));
        put32(central, quint32(e.data.size()));
        put16(central, quint16(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central.append(name);
    }
    const quint32 cdOffset = quint32(out.size());
    out.append(central);
    put32(out, 0x06054b50u);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(central.size()));
    put32(out, cdOffset);
    put16(out, 0);
    return out;
}

} // namespace zip
