#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QDataStream>
#include <iostream>
#include <QStringList>
#include <QDebug>
#include "chess/pgn_reader.h"
#include "chess/pgn_printer.h"
#include "chess/dcgencoder.h"
#include "chess/database.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName("pgn2pgn");
    QCoreApplication::setApplicationVersion("v1.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("pgn2pgn");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("source", QCoreApplication::translate("main", "PGN input file."));
    parser.addPositionalArgument("destination", QCoreApplication::translate("main", "PGN (Jerry Formatting) output file."));

    QCommandLineOption pgnFileOption(QStringList() << "p" << "pgn-file",
              QCoreApplication::translate("main", "PGN input file <games.pgn>."),
              QCoreApplication::translate("main", "filename."));
    parser.addOption(pgnFileOption);


    QCommandLineOption dbFileOption(QStringList() << "o" << "out-file",
              QCoreApplication::translate("main", "pgn out file <pgnfile.pgn."),
              QCoreApplication::translate("main", "filename."));
    parser.addOption(dbFileOption);


    parser.process(app);

    const QStringList args = parser.positionalArguments();
    // source pgn is args.at(0), destination filename is args.at(1)

    if (!parser.parse(QCoreApplication::arguments())) {
        QString errorMessage = parser.errorText();
        std::cout << errorMessage.toStdString() << std::endl;
        exit(0);
    }

    QString pgnFileName = parser.value(pgnFileOption);
    QFile pgnFile;
    pgnFile.setFileName(pgnFileName);
    if(!pgnFile.exists()) {
        std::cout << "Error: can't open PGN file or no PGN filename given." << std::endl;
        exit(0);
    }

    pgnFile.setFileName(pgnFileName);
    if(!pgnFile.exists()) {
        std::cout << "Error: can't open PGN file." << std::endl;
        exit(0);
    }

    QString dbFileName = parser.value(dbFileOption);
    if(dbFileName == pgnFileName) {
        std::cout << "Input and output file name are the same." << std::endl;
        exit(0);
    }
    if(dbFileName.isEmpty()) {
        std::cout << "Error: no output filename given." << std::endl;
        exit(0);
    }

    chess::PgnReader *pgnreader = new chess::PgnReader();

    // first scan offsets
    quint64 offset = 0;

    const char* encoding = pgnreader->detect_encoding(pgnFileName);

    QList<quint64> *offsets = new QList<quint64>();

    bool stop = false;
    while(!stop) {
        chess::HeaderOffset *header = new chess::HeaderOffset();
        delete header->headers;
        int res = pgnreader->readNextHeader(pgnFileName, encoding, &offset, header);
        if(res < 0) {
            stop = true;
            delete header;
            continue;
        }
        offsets->append(header->offset);
        header->headers->clear();
        delete header->headers;
        delete header;
    }
    chess::PgnPrinter *pp = new chess::PgnPrinter();
    QFile fOut(dbFileName);
    bool success = false;
    if(fOut.open(QFile::WriteOnly | QFile::Text)) {
        QTextStream s(&fOut);
        for(int i=0;i<offsets->count();i++) {
            quint64 offset_i = offsets->at(i);
            chess::Game *g = pgnreader->readGameFromFile(pgnFileName, encoding, offset_i);
            //g->headers = header_i->headers;
            QStringList *pgn = pp->printGame(g);
            for (int i = 0; i < pgn->size(); ++i) {
                s << pgn->at(i) << '\n';
            }
            delete g;
            pgn->clear();
            delete pgn;
            s << '\n' << '\n';
        }
        success = true;
    } else {
        std::cerr << "error opening output file\n";
    }
    fOut.close();
    if(!success) {
        throw std::invalid_argument("Error writing file");
    }

    offsets->clear();
    delete offsets;
    delete pgnreader;
    delete pp;
    return 0;
}

