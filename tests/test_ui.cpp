// Smoke tests for the widgets: build every view/dialog on the example project, exercise the main
// interactions and optionally save screenshots (set TESSERA_SHOTS to a directory).
#include "core/DemoProject.h"
#include "core/Project.h"
#include "ui/CodeTree.h"
#include "ui/DocumentTree.h"
#include "ui/DocumentView.h"
#include "ui/MainWindow.h"
#include "ui/MatrixDialog.h"
#include "ui/MemoManager.h"
#include "ui/RetrievalDialog.h"
#include "ui/SearchDialog.h"
#include "ui/SegmentsPanel.h"
#include "ui/VariablesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextEdit>

class TestUi : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    Project *m_project = nullptr;

    void shot(QWidget *w, const QString &name)
    {
        const QString dir = qEnvironmentVariable("TESSERA_SHOTS");
        if (!dir.isEmpty())
            w->grab().save(QDir(dir).filePath(name + QStringLiteral(".png")));
    }

    qint64 codeNamed(const QString &name) const
    {
        for (const Code &c : m_project->codes())
            if (c.name == name)
                return c.id;
        return 0;
    }

private slots:
    void initTestCase()
    {
        m_project = new Project(this);
        QVERIFY(m_project->create(m_dir.filePath("ui.tsra")));
        QVERIFY(DemoProject::populate(*m_project));
        QCOMPARE(m_project->documents().size(), 3);
        QCOMPARE(m_project->allSegments().size(), 11);
    }

    void trees()
    {
        DocumentTree docs(m_project);
        docs.rebuild();
        QCOMPARE(docs.topLevelItemCount(), 1);
        QCOMPARE(docs.topLevelItem(0)->childCount(), 3);
        docs.setAllActive(true);
        QCOMPARE(docs.activeDocumentIds().size(), 3);

        CodeTree codes(m_project);
        codes.rebuild();
        QCOMPARE(codes.topLevelItemCount(), 3);
        codes.setAllActive(true);
        QCOMPARE(codes.activeCodeIds().size(), 8);
    }

    void codingAndEditing()
    {
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        const qint64 doc = m_project->documents().first().id;
        view.openDocument(doc);
        QCOMPARE(view.documentId(), doc);

        const QString text = m_project->documentText(doc);
        const int pos = int(text.indexOf("tre anni"));
        QVERIFY(pos > 0);
        // Selection with a leading space is trimmed to the words.
        view.selectRange(pos - 1, pos + 8);
        const qint64 code = codeNamed("Flessibilità");
        const qint64 seg = view.codeSelection(code);
        QVERIFY(seg > 0);
        QCOMPARE(m_project->segment(seg).start, pos);
        QCOMPARE(m_project->segment(seg).end, pos + 8);
        shot(&view, "document_view");

        // Editing: text typed at the beginning shifts every segment.
        const QList<Segment> before = m_project->segmentsForDocument(doc);
        view.setEditing(true);
        QVERIFY(view.isEditing());
        auto *edit = view.findChild<QTextEdit *>();
        QVERIFY(edit);
        QTextCursor c(edit->document());
        c.setPosition(0);
        c.insertText("PREMESSA. ");
        QVERIFY(view.commitEdits());
        QVERIFY(!view.isEditing());
        QVERIFY(m_project->documentText(doc).startsWith("PREMESSA. Intervistatore"));
        const QList<Segment> after = m_project->segmentsForDocument(doc);
        QCOMPARE(after.size(), before.size());
        for (int i = 0; i < after.size(); ++i) {
            QCOMPARE(after[i].start, before[i].start + 10);
            QCOMPARE(after[i].end, before[i].end + 10);
        }
        // Segment text is unchanged by the shift.
        const Segment s = m_project->segment(seg);
        QCOMPARE(m_project->documentText(doc).mid(s.start, s.length()), QString("tre anni"));

        // Undoing the text edit updates the open view as well.
        QCOMPARE(m_project->undoLabel(), QString("Modifica testo"));
        QVERIFY(m_project->undo());
        QVERIFY(edit->toPlainText().startsWith("Intervistatore"));
        QCOMPARE(m_project->segment(seg).start, pos);
        // ...and undoing the coding removes it.
        QCOMPARE(m_project->undoLabel(), QString("Codifica"));
        QVERIFY(m_project->undo());
        QVERIFY(!m_project->segment(seg).id);
        QVERIFY(m_project->redo());
        QVERIFY(m_project->redo());
        QVERIFY(edit->toPlainText().startsWith("PREMESSA."));
    }

    void findInDocument()
    {
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        const qint64 doc = m_project->documents().first().id;
        view.openDocument(doc);
        auto *edit = view.findChild<QTextEdit *>();
        QVERIFY(edit);
        auto *field = view.findChild<QLineEdit *>();
        QVERIFY(field);
        QVERIFY(!view.isFindBarVisible());
        const int baseExtras = int(edit->extraSelections().size());

        // Ctrl+F opens the bar; typing counts and highlights the occurrences.
        view.activateWindow(); // shortcuts only fire in the active window
        QVERIFY(QTest::qWaitForWindowActive(&view));
        edit->setFocus();
        QTest::keyClick(edit, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(view.isFindBarVisible());
        QTest::keyClicks(field, "Giulia");
        const int n = view.findMatchCount();
        QVERIFY(n > 1);
        QCOMPARE(int(edit->extraSelections().size()), baseExtras + n);
        QCOMPARE(edit->textCursor().selectedText().toLower(), QString("giulia"));

        // F3 / Shift+F3 move and wrap around.
        const int first = edit->textCursor().selectionStart();
        QTest::keyClick(field, Qt::Key_F3);
        QVERIFY(edit->textCursor().selectionStart() != first);
        QTest::keyClick(field, Qt::Key_F3, Qt::ShiftModifier);
        QCOMPARE(edit->textCursor().selectionStart(), first);
        view.findPrevious();
        QVERIFY(edit->textCursor().selectionStart() > first); // wrapped to the last one

        // Whole word excludes "lavorativa"; case option is honoured.
        field->setText("lavor");
        const int partial = view.findMatchCount();
        auto *whole = view.findChildren<QCheckBox *>().last();
        whole->setChecked(true);
        QVERIFY(view.findMatchCount() < partial);
        whole->setChecked(false);
        field->setText("GIULIA");
        QCOMPARE(view.findMatchCount(), n);
        view.findChildren<QCheckBox *>().first()->setChecked(true);
        QCOMPARE(view.findMatchCount(), 0);
        view.findChildren<QCheckBox *>().first()->setChecked(false);

        // Esc closes the bar and removes the highlights.
        QTest::keyClick(field, Qt::Key_Escape);
        QVERIFY(!view.isFindBarVisible());
        QCOMPARE(view.findMatchCount(), 0);
        QCOMPARE(int(edit->extraSelections().size()), baseExtras);
    }

    void findWithEmoji()
    {
        const qint64 doc = m_project->addDocument(m_project->documents().first().groupId, QStringLiteral("Emoji"), QStringLiteral("a 😀 b 😀 casa 😀casa"));
        QVERIFY(doc > 0);
        DocumentView view(m_project);
        view.resize(800, 600);
        view.show();
        view.openDocument(doc);
        auto *edit = view.findChild<QTextEdit *>();
        auto *field = view.findChild<QLineEdit *>();
        view.showFindBar();
        field->setText(QStringLiteral("😀"));
        QCOMPARE(view.findMatchCount(), 3);
        // Positions are UTF-16 offsets: the 2nd emoji follows "a 😀 b ".
        view.findNext();
        QCOMPARE(edit->textCursor().selectionStart(), 7);
        QCOMPARE(edit->textCursor().selectionEnd(), 9);
        // "casa" next to an emoji still counts as a whole word.
        field->setText(QStringLiteral("casa"));
        view.findChildren<QCheckBox *>().last()->setChecked(true);
        QCOMPARE(view.findMatchCount(), 2);
        QVERIFY(m_project->deleteDocument(doc)); // keep the shared demo project as the other tests expect
    }

    void segmentsPanel()
    {
        SegmentsPanel panel(m_project);
        panel.resize(900, 400);
        panel.setActivation({}, {codeNamed("Isolamento")});
        QCOMPARE(panel.segments().size(), 2);
        shot(&panel, "segments_panel");
    }

    void complexRetrieval()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("retrieval.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", QString(40, 'x'));
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::blue);
        p.addSegment(doc, a, 0, 10);
        p.addSegment(doc, a, 20, 30);
        p.addSegment(doc, b, 5, 15);

        RetrievalDialog dlg(&p, {}, {a});
        SegmentsPanel panel(&p);
        panel.setActivation({}, {a});
        QCOMPARE(panel.segments().size(), 2);
        connect(&dlg, &RetrievalDialog::retrievalRequested, &panel, &SegmentsPanel::setRetrieval);
        dlg.show();

        // Mode combo: pick "Sovrapposizione" and B as the second list.
        QComboBox *mode = dlg.findChildren<QComboBox *>().first();
        mode->setCurrentIndex(mode->findData(int(Retrieval::Mode::Overlap)));
        const QList<QListWidget *> lists = dlg.findChildren<QListWidget *>();
        QCOMPARE(lists.size(), 2);
        for (int i = 0; i < lists[1]->count(); ++i)
            if (lists[1]->item(i)->data(Qt::UserRole).toLongLong() == b)
                lists[1]->item(i)->setCheckState(Qt::Checked);
        QCOMPARE(dlg.previewCount(), 1);

        QPushButton *apply = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Mostra nel pannello"))
                apply = btn;
        QVERIFY(apply && apply->isEnabled());
        apply->click();
        QVERIFY(panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 0);

        // Intersection yields a new interval that can be coded.
        mode->setCurrentIndex(mode->findData(int(Retrieval::Mode::Intersection)));
        for (int i = 0; i < lists[0]->count(); ++i)
            if (lists[0]->item(i)->data(Qt::UserRole).toLongLong() == b)
                lists[0]->item(i)->setCheckState(Qt::Checked);
        apply->click();
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 5);
        QCOMPARE(panel.segments().first().end, 10);
        shot(&panel, "complex_retrieval");

        const qint64 code = panel.codeResults("Sovrapposti");
        QVERIFY(code > 0);
        const QList<Segment> coded = p.segments({}, {code});
        QCOMPARE(coded.size(), 1);
        QCOMPARE(coded.first().start, 5);
        QCOMPARE(coded.first().end, 10);

        panel.clearRetrieval();
        QVERIFY(!panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 2);
    }

    void dialogs()
    {
        MatrixDialog matrix(m_project, {}, {});
        matrix.show();
        QTableWidget *table = nullptr;
        for (QTableWidget *t : matrix.findChildren<QTableWidget *>())
            if (t->columnCount() == int(m_project->documents().size()) + 1)
                table = t;
        QVERIFY(table);
        QCOMPARE(table->rowCount(), int(m_project->codes().size()) + 1);
        QCOMPARE(table->columnCount(), int(m_project->documents().size()) + 1);
        shot(&matrix, "matrix");

        VariablesDialog vars(m_project);
        vars.show();
        auto *vtable = vars.findChild<QTableWidget *>();
        QCOMPARE(vtable->columnCount(), 2 + int(m_project->variables().size()));
        shot(&vars, "variables");

        MemoManager memos(m_project);
        memos.show();
        QCOMPARE(memos.findChild<QTableWidget *>()->rowCount(), int(m_project->memos().size()));
        shot(&memos, "memos");

        const qint64 code = codeNamed("Strumenti digitali");
        SearchDialog search(m_project, [] { return QList<qint64>{}; }, [code] { return code; });
        search.show();
        auto *query = search.findChild<QLineEdit *>();
        query->setText("video OR Teams");
        QTest::keyClick(query, Qt::Key_Return);
        auto *results = search.findChild<QTableWidget *>();
        QCOMPARE(results->rowCount(), 3); // "videochiamata", "Teams", "video"
        shot(&search, "search");
    }

    void mainWindow()
    {
        MainWindow w;
        w.resize(1400, 860);
        w.show();
        QVERIFY(w.createExampleProject(m_dir.filePath("main.tsra")));
        w.showOverview();
        QTest::qWait(200);
        shot(&w, "main_window");
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
