#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QTimer>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("Tessera"));
    QApplication::setApplicationName(QStringLiteral("Tessera"));
    QApplication::setApplicationVersion(QStringLiteral(TESSERA_VERSION_STRING));

    // Standard Qt dialogs (file dialogs, Yes/No buttons) in the user's language.
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), QStringLiteral("qtbase"), QStringLiteral("_"),
                          QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Tessera - analisi qualitativa dei dati"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("progetto"), QStringLiteral("File di progetto .tsra da aprire"));
    QCommandLineOption exampleOption(QStringLiteral("example"),
                                     QStringLiteral("Crea un progetto di esempio nel percorso indicato e lo apre"),
                                     QStringLiteral("percorso"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Salva un'immagine della finestra ed esce (per i test)"),
                                        QStringLiteral("png"));
    parser.addOption(exampleOption);
    parser.addOption(screenshotOption);
    parser.process(app);

    MainWindow window;
    window.show();

    if (parser.isSet(exampleOption)) {
        if (window.createExampleProject(QDir::current().absoluteFilePath(parser.value(exampleOption))))
            window.showOverview();
    } else if (!parser.positionalArguments().isEmpty()) {
        window.openProject(parser.positionalArguments().first());
    } else {
        window.reopenLastProject();
    }

    if (!parser.isSet(screenshotOption))
        window.askUserNameOnFirstRun();

    if (parser.isSet(screenshotOption)) {
        window.resize(1400, 860);
        const QString out = parser.value(screenshotOption);
        QTimer::singleShot(1500, &window, [&window, out] {
            window.grab().save(out);
            QApplication::quit();
        });
    }
    return app.exec();
}
