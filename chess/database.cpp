#include "database.h"
#include "chess/game.h"
#include "chess/pgn_reader.h"
#include "chess/dcgencoder.h"
#include "chess/byteutil.h"
#include "assert.h"
#include <iostream>
#include <QFile>
#include <QDataStream>
#include <QDebug>

chess::Database::Database(QString &filename)
{
    this->filenameBase = filename;
    this->filenameGames = QString(filename).append(".dcg");
    this->filenameIndex = QString(filename).append(".dci");
    this->filenameNames = QString(filename).append(".dcn");
    this->filenameSites = QString(filename).append(".dcs");
    this->magicNameString = QByteArrayLiteral("\x53\x69\x6d\x70\x6c\x65\x43\x44\x62\x6e");
    this->magicIndexString = QByteArrayLiteral("\x53\x69\x6d\x70\x6c\x65\x43\x44\x62\x69");
    this->magicGamesString = QByteArrayLiteral("\x53\x69\x6d\x70\x6c\x65\x43\x44\x62\x67");
    this->magicSitesString = QByteArrayLiteral("\x53\x69\x6d\x70\x6c\x65\x43\x44\x62\x73");
    this->offsetNames = new QMap<quint32, QString>();
    this->offsetSites = new QMap<quint32, QString>();
    this->dcgencoder = new chess::DcgEncoder();
    this->pgnreader = new chess::PgnReader();

    this->indices = new QList<chess::IndexEntry*>();
}

chess::Database::~Database()
{
    this->offsetNames->clear();
    this->offsetSites->clear();
    delete this->offsetNames;
    delete this->offsetSites;
    delete this->dcgencoder;
    delete this->pgnreader;
    delete this->indices;
}

void chess::Database::importPgnAndSave(QString &pgnfile) {

    QMap<QString, quint32> *names = new QMap<QString, quint32>();
    QMap<QString, quint32> *sites = new QMap<QString, quint32>();

    this->importPgnNamesSites(pgnfile, names, sites);
    this->importPgnAppendSites(sites);
    this->importPgnAppendNames(names);
    this->importPgnAppendGamesIndices(pgnfile, names, sites);

    delete names;
    delete sites;
}

void chess::Database::loadIndex() {

    this->indices->clear();
    QFile dciFile;
    dciFile.setFileName(this->filenameIndex);
    if(!dciFile.exists()) {
        std::cout << "Error: can't open .dci file." << std::endl;
    } else {
        // read index file into QList of IndexEntries
        // (basically memory mapping file)
        dciFile.open(QFile::ReadOnly);
        QDataStream gi(&dciFile);
        bool error = false;
        QByteArray magic;
        magic.resize(10);
        magic.fill(char(0x20));
        gi.readRawData(magic.data(), 10);
        if(QString::fromUtf8(magic) != this->magicIndexString) {
            error = true;
        }
        while(!gi.atEnd() && !error) {
            QByteArray idx;
            idx.resize(35);
            idx.fill(char(0x00));
            if(gi.readRawData(idx.data(), 35) < 0) {
                error = true;
                continue;
            }
            QDataStream ds_entry_i(idx);
            chess::IndexEntry *entry_i = new chess::IndexEntry();

            quint8 status;
            ds_entry_i >> status;
            qDebug() << "status: " << status;
            if(status == GAME_DELETED) {
                entry_i->deleted = true;
            } else {
                entry_i->deleted = false;
            }
            ds_entry_i >> entry_i->gameOffset;
            qDebug() << "gameOffset: " << entry_i->gameOffset;
            ds_entry_i >> entry_i->whiteOffset;
            qDebug() << "whiteOffset: " << entry_i->whiteOffset;
            ds_entry_i >> entry_i->blackOffset;
            qDebug() << "whiteOffset: " << entry_i->blackOffset;
            ds_entry_i >> entry_i->round;
            qDebug() << "round: " << entry_i->round;
            ds_entry_i >> entry_i->siteRef;
            qDebug() << "site ref: " << entry_i->siteRef;
            ds_entry_i >> entry_i->eloWhite;
            qDebug() << "eloWhite: " << entry_i->eloWhite;
            ds_entry_i >> entry_i->eloBlack;
            qDebug() << "eloBlack: " << entry_i->eloBlack;
            ds_entry_i >> entry_i->result;
            qDebug() << "result: " << entry_i->result;
            char *eco = new char[sizeof "A00"];
            ds_entry_i.readRawData(eco, 3);
            entry_i->eco = eco;
            qDebug() << QString::fromLocal8Bit(eco);
            ds_entry_i >> entry_i->year;
            ds_entry_i >> entry_i->month;
            ds_entry_i >> entry_i->day;
            qDebug() << "yy.mm.dd " << entry_i->year << entry_i->month << entry_i->day;
            this->indices->append(entry_i);
        }
        dciFile.close();
    }
}

void chess::Database::loadSites() {
    this->offsetSites->clear();
    // for name file and site file build QMaps to quickly
    // access the data
    // read index file into QList of IndexEntries
    // (basically memory mapping file)
    QFile dcsFile;
    dcsFile.setFileName(this->filenameSites);
    dcsFile.open(QFile::ReadOnly);
    QDataStream ss(&dcsFile);
    bool error = false;
    QByteArray magic;
    magic.resize(10);
    magic.fill(char(0x20));
    ss.readRawData(magic.data(), 10);
    if(QString::fromUtf8(magic) != this->magicSitesString) {
        error = true;
    }
    while(!ss.atEnd() && !error) {
        quint32 pos = quint32(dcsFile.pos());
        QByteArray site_bytes;
        site_bytes.resize(36);
        site_bytes.fill(char(0x20));
        if(ss.readRawData(site_bytes.data(), 36) < 0) {
            error = true;
            break;
        }
        QString site = QString::fromUtf8(site_bytes).trimmed();
        this->offsetSites->insert(pos, site);
    }
    dcsFile.close();

}

int chess::Database::countGames() {
    return this->indices->length();
}

chess::Game* chess::Database::getGameAt(int i) {

    if(i >= this->indices->size()) {
        return 0; // maybe throw out of range error or something instead of silently failing
    }
    chess::IndexEntry *ie = this->indices->at(i);
    if(ie->deleted) {
        // todo: jump to next valid entry
    }
    chess::Game* game = new chess::Game();
    QString whiteName = this->offsetNames->value(ie->whiteOffset);
    QString blackName = this->offsetNames->value(ie->blackOffset);
    QString site = this->offsetSites->value(ie->siteRef);
    game->headers->insert("White",whiteName);
    game->headers->insert("Black", blackName);
    game->headers->insert("Site", site);
    QString date("");
    if(ie->year != 0) {
        date.append(QString::number(ie->year));
    } else {
        date.append("????");
    }
    date.append(".");
    if(ie->month != 0) {
        date.append(QString::number(ie->month));
    } else {
        date.append("??");
    }
    date.append(".");
    if(ie->day != 0) {
        date.append(QString::number(ie->day));
    } else {
        date.append("??");
    }
    game->headers->insert("Date", date);
    if(ie->result == RES_WHITE_WINS) {
        game->headers->insert("Result", "1-0");
    } else if(ie->result == RES_BLACK_WINS) {
        game->headers->insert("Result", "0-1");
    } else if(ie->result == RES_DRAW) {
        game->headers->insert("Result", "1/2-1/2");
    } else {
        game->headers->insert("Result", "*");
    }
    game->headers->insert("ECO", ie->eco);
    if(ie->round != 0) {
        game->headers->insert("Round", QString::number((ie->round)));
    } else {
        game->headers->insert("Round", "?");
    }
    QFile fnGames(this->filenameGames);
    if(fnGames.open(QFile::ReadOnly)) {
        fnGames.seek(ie->gameOffset);
        QDataStream gi(&fnGames);
        int length = this->decodeLength(&gi);
        QByteArray game_raw;
        game_raw.resize(length);
        game_raw.fill(char(0x20));
        gi.readRawData(game_raw.data(), length);
        qDebug() << "length" << length << " arr: " << game_raw.toHex();
        this->dcgdecoder->decodeGame(game, &game_raw);
    }
    return game;
}

int chess::Database::decodeLength(QDataStream *stream) {
    quint8 len1 = 0;
    *stream >> len1;
    qDebug() << "len1 is: " << len1;
    if(len1 < 127) {
        return int(len1);
    }
    if(len1 == 0x81) {
        quint8 len2 = 0;
        *stream >> len2;
        return int(len2);
    }
    if(len1 == 0x82) {
        quint16 len2 = 0;
        *stream >> len2;
        return int(len2);
    }
    if(len1 == 0x83) {
        quint8 len2=0;
        quint16 len3=0;
        *stream >> len2;
        *stream >> len3;
        quint32 ret = 0;
        ret = len2 << 16;
        ret = ret + len3;
        return ret;
    }
    if(len1 == 0x84) {
        quint32 len = 0;
        *stream >> len;
        return int(len);
    }
    throw std::invalid_argument("length decoding called with illegal byte value");
}

void chess::Database::loadNames() {

    this->offsetNames->clear();
    QFile dcnFile;
    dcnFile.setFileName(this->filenameNames);
    dcnFile.open(QFile::ReadOnly);
    QDataStream sn(&dcnFile);
    bool error = false;
    QByteArray magic;
    magic.resize(10);
    magic.fill(char(0x20));
    sn.readRawData(magic.data(), 10);
    if(QString::fromUtf8(magic) != this->magicNameString) {
        error = true;
    }
    while(!sn.atEnd() && !error) {
        quint32 pos = quint32(dcnFile.pos());
        QByteArray name_bytes;
        name_bytes.resize(36);
        name_bytes.fill(char(0x20));
        if(sn.readRawData(name_bytes.data(), 36) < 0) {
            error = true;
            break;
        }
        QString name = QString::fromUtf8(name_bytes).trimmed();
        this->offsetNames->insert(pos, name);
    }
    dcnFile.close();
}

void chess::Database::importPgnNamesSites(QString &pgnfile, QMap<QString, quint32> *names, QMap<QString, quint32> *sites) {

    std::cout << "scanning names and sites from " << pgnfile.toStdString() << std::endl;
    const char* encoding = pgnreader->detect_encoding(pgnfile);

    chess::HeaderOffset* header = new chess::HeaderOffset();

    quint64 offset = 0;
    bool stop = false;

    std::cout << "scanning at 0";
    int i = 0;
    while(!stop) {
        if(i%100==0) {
            std::cout << "\rscanning at " << offset;
        }
        i++;
        int res = pgnreader->readNextHeader(pgnfile, encoding, &offset, header);
        if(res < 0) {
            stop = true;
            continue;
        }
        // below 4294967295 is the max range val of quint32
        // provided as default key during search. In case we get this
        // default key as return, the current db site and name maps do not contain
        // a value. Note that this limits the offset range of name and site
        // file to 4294967295-1!
        // otherwise we add the key index of the existing database map files
        // these must then be skipped when writing the newly read sites and names
        if(header->headers != 0) {
            if(header->headers->contains("Site")) {
                QString site = header->headers->value("Site");
                quint32 key = this->offsetSites->key(site, 4294967295);
                if(key == 4294967295) {
                    sites->insert(header->headers->value("Site"), 0);
                } else {
                    sites->insert(header->headers->value("Site"), key);
                }
            }
            if(header->headers->contains("White")) {
                QString white = header->headers->value("White");
                quint32 key = this->offsetNames->key(white, 4294967295);
                if(key == 4294967295) {
                    names->insert(header->headers->value("White"), 0);
                } else {
                    names->insert(header->headers->value("White"), key);
                }
            }
            if(header->headers->contains("Black")) {
                QString black = header->headers->value("Black");
                quint32 key = this->offsetNames->key(black, 4294967295);
                if(key == 4294967295) {
                    names->insert(header->headers->value("Black"), 0);
                } else {
                    names->insert(header->headers->value("Black"), key);
                }
            }
        }
        header->headers->clear();
        if(header->headers!=0) {
           delete header->headers;
        }
    }
    std::cout << std::endl << "scanning finished" << std::flush;
    delete header;
}

// save the map at the _end_ of file with filename (i.e. apend new names or sites)
// update the offset while saving
void chess::Database::importPgnAppendNames(QMap<QString, quint32> *names) {
    QFile fnNames(this->filenameNames);
    if(fnNames.open(QFile::Append)) {
        if(fnNames.pos() == 0) {
            fnNames.write(this->magicNameString, this->magicNameString.length());
        }
        QList<QString> keys = names->keys();
        for (int i = 0; i < keys.length(); i++) {
            // value != 0 means the value exists
            // already in the existing database maps
            quint32 ex_offset = names->value(keys.at(i));
            if(ex_offset != 0) {
                continue;
            }
            QByteArray name_i = keys.at(i).toUtf8();
            // truncate if too long
            if(name_i.size() > 36) {
                name_i = name_i.left(36);
            }
            // pad to 36 byte if necc.
            int pad_n = 36 - name_i.length();
            if(pad_n > 0) {
                for(int j=0;j<pad_n;j++) {
                    name_i.append(0x20);
                }
            }
        quint32 offset = fnNames.pos();
        fnNames.write(name_i,36);
        names->insert(name_i, offset);
        }
    }
    fnNames.close();
}

// save the map at the _end_ of file with filename (i.e. apend new names or sites)
// update the offset while saving
void chess::Database::importPgnAppendSites(QMap<QString, quint32> *sites) {
    QFile fnSites(this->filenameSites);
    if(fnSites.open(QFile::Append)) {
        if(fnSites.pos() == 0) {
            fnSites.write(this->magicNameString, this->magicNameString.length());
        }
        QList<QString> keys = sites->keys();
        for (int i = 0; i < keys.length(); i++) {
            // value != 0 means the value exists
            // already in the existing database maps
            quint32 ex_offset = sites->value(keys.at(i));
            if(ex_offset != 0) {
                continue;
            }
            QByteArray site_i = keys.at(i).toUtf8();
            // truncate if too long
            if(site_i.size() > 36) {
                site_i = site_i.left(36);
            }
            // pad to 36 byte if necc.
            int pad_n = 36 - site_i.length();
            if(pad_n > 0) {
                for(int j=0;j<pad_n;j++) {
                    site_i.append(0x20);
                }
            }
        quint32 offset = fnSites.pos();
        fnSites.write(site_i,36);
        sites->insert(site_i, offset);
        }
    }
    fnSites.close();
}

void chess::Database::importPgnAppendGamesIndices(QString &pgnfile, QMap<QString, quint32> *names, QMap<QString, quint32> *sites) {

    // now save everything
    chess::HeaderOffset *header = new chess::HeaderOffset();
    quint64 offset = 0;
    QFile pgnFile(pgnfile);
    quint64 size = pgnFile.size();
    bool stop = false;

    const char* encoding = this->pgnreader->detect_encoding(pgnfile);

    QFile fnIndex(this->filenameIndex);
    QFile fnGames(this->filenameGames);
    if(fnIndex.open(QFile::Append)) {
        if(fnGames.open(QFile::Append)) {
            if(fnIndex.pos() == 0) {
                fnIndex.write(this->magicIndexString, this->magicIndexString.length());
            }
            if(fnGames.pos() == 0) {
                fnGames.write(magicGamesString, magicGamesString.length());
            }

            std::cout << "\nsaving games: 0/"<< size;
            int i = 0;
            while(!stop) {
                if(i%100==0) {
                    std::cout << "\rsaving games: "<<offset<< "/"<<size << std::flush;
                }
                i++;
                int res = pgnreader->readNextHeader(pgnfile, encoding, &offset, header);
                if(res < 0) {
                    stop = true;
                    continue;
                }
                // the current index entry
                QByteArray iEntry;
                // first write index entry
                // status
                ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                // game offset
                ByteUtil::append_as_uint64(&iEntry, fnGames.pos());
                // white offset
                QString white = header->headers->value("White");
                quint32 whiteOffset = names->value(white);
                ByteUtil::append_as_uint32(&iEntry, whiteOffset);
                // black offset
                QString black = header->headers->value("Black");
                quint32 blackOffset = names->value(black);
                ByteUtil::append_as_uint32(&iEntry, blackOffset);
                // round
                quint16 round = header->headers->value("Round").toUInt();
                ByteUtil::append_as_uint16(&iEntry, round);
                // site offset
                quint32 site_offset = sites->value(header->headers->value("Site"));
                ByteUtil::append_as_uint32(&iEntry, site_offset);
                // elo white
                quint16 elo_white = header->headers->value("Elo White").toUInt();
                ByteUtil::append_as_uint16(&iEntry, elo_white);
                quint16 elo_black = header->headers->value("Elo White").toUInt();
                ByteUtil::append_as_uint16(&iEntry, elo_black);
                // result
                if(header->headers->contains("Result")) {
                    QString res = header->headers->value("Result");
                    if(res == "1-0") {
                        ByteUtil::append_as_uint8(&iEntry, quint8(0x01));
                    } else if(res == "0-1") {
                        ByteUtil::append_as_uint8(&iEntry, quint8(0x02));
                    } else if(res == "1/2-1/2") {
                        ByteUtil::append_as_uint8(&iEntry, quint8(0x03));
                    } else {
                        ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                    }
                } else  {
                    ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                }
                // ECO
                if(header->headers->contains("ECO")) {
                    QByteArray eco = header->headers->value("ECO").toUtf8();
                    iEntry.append(eco);
                } else {
                    QByteArray eco = QByteArrayLiteral("\x00\x00\x00");
                    iEntry.append(eco);
                }
                // parse date
                if(header->headers->contains("Date")) {
                    QString date = header->headers->value("Date");
                    // try to parse the date
                    quint16 year = 0;
                    quint8 month = 0;
                    quint8 day = 0;
                    QStringList dd_mm_yy = date.split(".");
                    if(dd_mm_yy.size() > 0 && dd_mm_yy.at(0).length() == 4) {
                        quint16 prob_year = dd_mm_yy.at(0).toInt();
                        if(prob_year > 0 && prob_year < 2100) {
                            year = prob_year;
                        }
                        if(dd_mm_yy.size() > 1 && dd_mm_yy.at(1).length() == 2) {
                            quint16 prob_month = dd_mm_yy.at(1).toInt();
                            if(prob_year > 0 && prob_year <= 12) {
                                month = prob_month;
                            }
                            if(dd_mm_yy.size() > 2 && dd_mm_yy.at(2).length() == 2) {
                            quint16 prob_day = dd_mm_yy.at(2).toInt();
                            if(prob_year > 0 && prob_year < 32) {
                                day = prob_day;
                                }
                            }
                        }
                    }
                    ByteUtil::append_as_uint16(&iEntry, year);
                    ByteUtil::append_as_uint8(&iEntry, month);
                    ByteUtil::append_as_uint8(&iEntry, day);
                } else {
                    ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                    ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                    ByteUtil::append_as_uint8(&iEntry, quint8(0x00));
                }
                assert(iEntry.size() == 35);
                fnIndex.write(iEntry, iEntry.length());
                //qDebug() << "just before reading back file";
                chess::Game *g = pgnreader->readGameFromFile(pgnfile, encoding, header->offset);
                //qDebug() << "READ file ok";
                QByteArray *g_enc = dcgencoder->encodeGame(g); //"<<<<<<<<<<<<<<<<<<<<<< this is the cause of mem acc fault"
                //qDebug() << "enc ok";
                fnGames.write(*g_enc, g_enc->length());
                delete g_enc;
                header->headers->clear();
                if(header->headers!=0) {
                    delete header->headers;
                }
                delete g;
            }
            std::cout << "\rsaving games: "<<size<< "/"<<size << std::endl;
        }
        fnGames.close();
    }
    fnIndex.close();
    delete header;
}



/*
 write sites into file
QString fnSitesString = pgnFileName.left(pgnFileName.size()-3).append("dcs");
QFile fnSites(fnSitesString);
QByteArray magicSiteString = QByteArrayLiteral("\x53\x69\x6d\x70\x6c\x65\x43\x44\x62\x73");
success = false;
if(fnSites.open(QFile::WriteOnly)) {
  QDataStream s(&fnSites);
  s.writeRawData(magicSiteString, magicSiteString.length());
  QList<QString> keys = sites->keys();
  for (int i = 0; i < keys.length(); i++) {
      QByteArray site_i = keys.at(i).toUtf8();
      // truncate if too long
      if(site_i.size() > 36) {
          site_i = site_i.left(36);
      }
      int pad_n = 36 - site_i.length();
      if(pad_n > 0) {
          for(int j=0;j<pad_n;j++) {
              site_i.append(0x20);
          }
      }
      quint32 offset = fnSites.pos();
      s.writeRawData(site_i,36);
      sites->insert(site_i, offset);
  }
  success = true;
} else {
  std::cerr << "error opening output file\n";
}
fnNames.close();
if(!success) {
    throw std::invalid_argument("Error writing file");
}
pgnFile.close();
*/

    /*
void chess::Database::saveToFile() {

}

void chess::Database::writeSites() {

}

void chess::Database::writeNames() {

}

void chess::Database::writeIndex() {

}

void chess::Database::writeGames() {

}

*/
