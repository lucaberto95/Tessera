// Smoke tests for the widgets: build every view/dialog on the example project, exercise the main
// interactions and optionally save screenshots (set TESSERA_SHOTS to a directory).
#include "core/DemoProject.h"
#include "core/Project.h"
#include "ui/CodeTree.h"
#include "ui/AgreementDialog.h"
#include "ui/ReportDialog.h"
#include "ui/CooccurrenceDialog.h"
#include "ui/DictionaryDialog.h"
#include "ui/StatsDialog.h"
#include "ui/SummaryGridDialog.h"
#include "ui/SummaryTablesDialog.h"
#include "ui/DocumentTree.h"
#include "core/RichText.h"
#include <QSvgRenderer>
#include <QtEndian>
#include <QTextBlock>
#include <QTextList>
#include "ui/DocumentView.h"
#include "core/Pdfs.h"
#include "ui/ImageView.h"
#include "ui/PdfView.h"
#include "ui/LinksDialog.h"
#include "ui/MainWindow.h"
#include "ui/MatrixDialog.h"
#include "ui/MemoDialog.h"
#include "ui/MemoManager.h"
#include "ui/MergeDialog.h"
#include "ui/UiUtils.h"
#include "ui/RetrievalDialog.h"
#include "ui/SearchDialog.h"
#include "ui/SegmentsPanel.h"
#include "ui/SurveyImportDialog.h"
#include "ui/VariableFilterDialog.h"
#include "ui/VariablesDialog.h"
#include "ui/WordFrequencyDialog.h"

#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QWheelEvent>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTextEdit>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUrl>

#include <memory>

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

    // A4 PDF with real text: the lines of each page are drawn from y = 100 pt, 40 pt apart (72 dpi: units are points).
    static QByteArray makePdf(const QList<QStringList> &pages)
    {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setResolution(72);
        QPainter painter(&writer);
        painter.setFont(QFont(QStringLiteral("Arial"), 14));
        for (int p = 0; p < pages.size(); ++p) {
            if (p > 0)
                writer.newPage();
            for (int i = 0; i < pages[p].size(); ++i)
                painter.drawText(QPointF(72, 100 + 40 * i), pages[p][i]);
        }
        painter.end();
        return bytes;
    }
    qint64 codeNamed(const QString &name) const
    {
        for (const Code &c : m_project->codes())
            if (c.name == name)
                return c.id;
        return 0;
    }

private slots:
    void mergeDialogShowsReportAndOptions()
    {
        const QString fileA = m_dir.filePath("merge-a.tsra"), fileB = m_dir.filePath("merge-b.tsra");
        Project a;
        a.setAuthor("Anna");
        QVERIFY(a.create(fileA));
        const qint64 doc = a.addDocument(a.groups().first().id, "Testo", "Una frase da codificare bene.");
        const qint64 code = a.addCode(0, "Tema", Qt::red);
        a.addSegment(doc, code, 0, 3);
        QVERIFY(a.saveCopyAs(fileB));
        {
            Project b;
            b.setAuthor("Bruno");
            QVERIFY(b.open(fileB));
            b.addSegment(doc, b.findCode(0, "Tema"), 4, 9);
        }
        Merge::Source source;
        QString error;
        QVERIFY2(source.load(fileB, &error), qPrintable(error));
        MergeDialog dialog(&a, &source);
        auto *text = dialog.findChild<QPlainTextEdit *>();
        QVERIFY(text);
        QVERIFY(text->toPlainText().contains("Codifiche"));
        QCOMPARE(dialog.report().segmentsNew, 1);
        QCOMPARE(dialog.options().onlyAuthor, QString());
        const auto combos = dialog.findChildren<QComboBox *>();
        QCOMPARE(combos.size(), 2);
        combos[0]->setCurrentIndex(combos[0]->findText("Anna")); // only Anna's: nothing new
        QCOMPARE(dialog.options().onlyAuthor, QString("Anna"));
        QCOMPARE(dialog.report().segmentsNew, 0);
        QVERIFY(text->toPlainText().contains("Non c'è nulla"));
        QVERIFY(dialog.findChild<QDialogButtonBox *>());
        for (QPushButton *b : dialog.findChildren<QPushButton *>())
            if (b->text() == "Unisci")
                QVERIFY(!b->isEnabled());
        combos[0]->setCurrentIndex(0);
        for (QPushButton *b : dialog.findChildren<QPushButton *>())
            if (b->text() == "Unisci")
                QVERIFY(b->isEnabled());
        QCOMPARE(a.allSegments().size(), 1); // nothing was written by looking
    }
    void initTestCase()
    {
        m_project = new Project(this);
        QVERIFY(m_project->create(m_dir.filePath("ui.tsra")));
        QVERIFY(DemoProject::populate(*m_project));
        QCOMPARE(m_project->documents().size(), 3);
        QCOMPARE(m_project->allSegments().size(), 11);
        // The example dictionary finds something in every category.
        QCOMPARE(m_project->dictionaries().size(), 1);
        const Dictionaries::Analysis a = Dictionaries::analyze(*m_project, m_project->dictionaries().first().id);
        QCOMPARE(a.categories.size(), 4);
        for (const DictCategory &c : a.categories)
            QVERIFY2(a.total(c.id) > 0, qPrintable(c.name));
    }

    void trees()
    {
        DocumentTree docs(m_project);
        docs.rebuild();
        QCOMPARE(docs.topLevelItemCount(), 2); // the group, then "Set di documenti"
        QCOMPARE(docs.topLevelItem(0)->childCount(), 3);
        QCOMPARE(docs.topLevelItem(1)->childCount(), 1);
        QCOMPARE(docs.topLevelItem(1)->child(0)->childCount(), 2);
        docs.setAllActive(true);
        QCOMPARE(docs.activeDocumentIds().size(), 3); // each document once, even if also in a set

        // Checking a set activates its documents; their entries in the group follow.
        docs.setAllActive(false);
        QTreeWidgetItem *group = docs.topLevelItem(0);
        QTreeWidgetItem *set = docs.topLevelItem(1)->child(0);
        set->setCheckState(0, Qt::Checked);
        QCOMPARE(docs.activeDocumentIds().size(), 2);
        QCOMPARE(group->child(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(group->child(1)->checkState(0), Qt::Checked);
        QCOMPARE(group->child(2)->checkState(0), Qt::Checked);
        // ...and unchecking a document in its group unchecks it in the set.
        group->child(1)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(set->child(0)->checkState(0), Qt::Unchecked);
        QCOMPARE(set->checkState(0), Qt::PartiallyChecked);
        QCOMPARE(docs.activeDocumentIds().size(), 1);

        // A set created from the active documents.
        const qint64 newSet = m_project->addDocumentSet("Attivi", docs.activeDocumentIds());
        docs.rebuild();
        QCOMPARE(docs.topLevelItem(1)->childCount(), 2);
        QVERIFY(m_project->undo());
        QVERIFY(m_project->documentSets().size() == 1 && newSet > 0);
        docs.setAllActive(true);
        shot(&docs, "document_tree");

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

    void codingFromElsewhereWhileEditing()
    {
        // A separate project: the shared one is used by later tests.
        Project p;
        QVERIFY(p.create(m_dir.filePath("editing.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "Doc", "uno due tre quattro");
        const qint64 code = p.addCode(0, "C", Qt::red);
        const qint64 kept = p.addSegment(doc, code, 0, 3); // "uno"
        const qint64 gone = p.addSegment(doc, code, 4, 7); // "due"

        DocumentView view(&p);
        view.resize(600, 400);
        view.show();
        view.openDocument(doc);
        view.setEditing(true);
        auto *edit = view.findChild<QTextEdit *>();
        QTextCursor c(edit->document());
        c.setPosition(0);
        c.insertText("XX ");
        // Automatic coding while the text is being edited: positions refer to the stored text.
        const qint64 added = p.addSegment(doc, code, 8, 11); // "tre"
        QVERIFY(added > 0);
        QVERIFY(p.deleteSegment(gone));
        QVERIFY(view.commitEdits());

        const QString text = p.documentText(doc);
        QCOMPARE(text, QString("XX uno due tre quattro"));
        QCOMPARE(text.mid(p.segment(kept).start, p.segment(kept).length()), QString("uno"));
        QCOMPARE(text.mid(p.segment(added).start, p.segment(added).length()), QString("tre"));
        QVERIFY(!p.segment(gone).id);
        QCOMPARE(p.segmentsForDocument(doc).size(), 2);
    }

    void formattedDocumentView()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("formatted.tsra")));
        const QString text = "Titolo\nUn testo forte qui\nvoce uno\nA\nB";
        const qint64 doc = p.addDocument(p.groups().first().id, "Doc", text);
        RichText::Formatting f;
        f.blocks.append({0, 6, RichText::BlockKind::Heading, 1});
        f.blocks.append({26, 34, RichText::BlockKind::Bullet, 0});
        f.blocks.append({35, 36, RichText::BlockKind::TableCell, 2});
        const int forte = int(text.indexOf("forte"));
        f.spans.append({forte, forte + 5, RichText::Bold});
        QVERIFY(p.setDocumentRichContent(doc, RichText::toJson(f)));
        const qint64 code = p.addCode(0, "C", Qt::red);
        const qint64 seg = p.addSegment(doc, code, forte, forte + 5);

        DocumentView view(&p);
        view.resize(600, 400);
        view.show();
        view.openDocument(doc);
        auto *edit = view.findChild<QTextEdit *>();
        // Formatted, and still the same characters: positions of segments are unchanged.
        QCOMPARE(edit->document()->toRawText().replace(QChar::ParagraphSeparator, '\n'), text);
        QCOMPARE(edit->document()->findBlock(0).blockFormat().headingLevel(), 1);
        QVERIFY(edit->document()->findBlock(26).textList());
        QTextCursor c(edit->document());
        c.setPosition(forte + 2);
        QVERIFY(c.charFormat().fontWeight() >= QFont::Bold);
        QCOMPARE(p.segment(seg).start, forte);
        QCOMPARE(QString(edit->document()->toRawText()).replace(QChar::ParagraphSeparator, '\n').mid(p.segment(seg).start, p.segment(seg).length()), QString("forte"));

        // Editing keeps the formatting attached to the text that moved.
        view.setEditing(true);
        QTextCursor ins(edit->document());
        ins.setPosition(0);
        ins.insertText("XX ");
        QVERIFY(view.commitEdits());
        QCOMPARE(p.documentText(doc).left(9), QString("XX Titolo"));
        QCOMPARE(edit->document()->toRawText().replace(QChar::ParagraphSeparator, '\n'), p.documentText(doc));
        const int moved = int(p.documentText(doc).indexOf("forte"));
        QCOMPARE(p.segment(seg).start, moved);
        QCOMPARE(edit->document()->findBlock(0).blockFormat().headingLevel(), 1);
        c.setPosition(moved + 2);
        QVERIFY(c.charFormat().fontWeight() >= QFont::Bold);
    }
    void documentLinks()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("links.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 a = p.addDocument(g, "A", "Il lavoro da remoto mi isola dai colleghi.");
        const qint64 b = p.addDocument(g, "B", "Con il lavoro da remoto vedo di più la famiglia.");
        const QString textA = p.documentText(a), textB = p.documentText(b);

        DocumentView view(&p);
        view.resize(700, 400);
        view.show();
        QSignalSpy followed(&view, &DocumentView::linkFollowed);
        auto *edit = view.findChild<QTextEdit *>();

        // Start in A (surrounding spaces are trimmed), complete in B.
        view.openDocument(a);
        const int isola = int(textA.indexOf("mi isola"));
        view.selectRange(isola - 1, isola + 8);
        QVERIFY(!view.hasPendingLink());
        view.startLink();
        QVERIFY(view.hasPendingLink());
        view.openDocument(b);
        const int famiglia = int(textB.indexOf("la famiglia"));
        view.selectRange(famiglia, famiglia + 11);
        const qint64 link = view.completeLink();
        QVERIFY(link > 0);
        QVERIFY(!view.hasPendingLink());
        const DocumentLink l = p.link(link);
        QCOMPARE(l.fromDocumentId, a);
        QCOMPARE(textA.mid(l.fromStart, l.fromEnd - l.fromStart), QString("mi isola"));
        QCOMPARE(l.toDocumentId, b);
        QCOMPARE(textB.mid(l.toStart, l.toEnd - l.toStart), QString("la famiglia"));

        // The linked text is marked in both documents.
        QCOMPARE(view.links().size(), 1);
        const int extrasWithLink = int(edit->extraSelections().size());
        QCOMPARE(view.linksAt(famiglia + 3).size(), 1);
        QVERIFY(view.linksAt(famiglia - 2).isEmpty());
        shot(&view, "document_link");

        // Ctrl+click on the link in B opens the other end in A, selected.
        QTextCursor c(edit->document());
        c.setPosition(famiglia + 3);
        const QPoint inLink = edit->cursorRect(c).center() + QPoint(2, 0);
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::ControlModifier, inLink);
        QCOMPARE(view.documentId(), a);
        QCOMPARE(followed.count(), 1);
        QCOMPARE(followed.last().first().toLongLong(), a);
        QCOMPARE(edit->textCursor().selectedText(), QString("mi isola"));
        // ...and back, through the API used by the context menu.
        QVERIFY(view.followLinkAt(l.fromStart + 1));
        QCOMPARE(view.documentId(), b);
        QCOMPARE(edit->textCursor().selectedText(), QString("la famiglia"));
        // A plain click is an ordinary click.
        QTest::mouseClick(edit->viewport(), Qt::LeftButton, Qt::NoModifier, inLink);
        QCOMPARE(view.documentId(), b);

        // Editing A: the end follows its words.
        view.openDocument(a);
        view.setEditing(true);
        QTextCursor e(edit->document());
        e.setPosition(0);
        e.insertText("Premessa. ");
        QVERIFY(view.commitEdits());
        const DocumentLink moved = p.link(link);
        QCOMPARE(p.documentText(a).mid(moved.fromStart, moved.fromEnd - moved.fromStart), QString("mi isola"));
        QCOMPARE(moved.toStart, l.toStart);

        // The links window lists it, opens either end and deletes it.
        LinksDialog dlg(&p);
        dlg.show();
        auto *table = dlg.findChild<QTableWidget *>();
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QString("A"));
        QCOMPARE(table->item(0, 3)->text(), QString("la famiglia"));
        QSignalSpy ends(&dlg, &LinksDialog::endActivated);
        dlg.selectLink(link);
        QCOMPARE(dlg.currentLinkId(), link);
        emit table->cellDoubleClicked(0, 3);
        QCOMPARE(ends.count(), 1);
        QCOMPARE(ends.last().at(0).toLongLong(), b);
        QCOMPARE(ends.last().at(1).toInt(), l.toStart);
        dlg.openCurrentEnd(false);
        QCOMPARE(ends.last().at(0).toLongLong(), a);
        QCOMPARE(ends.last().at(1).toInt(), moved.fromStart);
        shot(&dlg, "links");

        QPushButton *remove = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Elimina"))
                remove = btn;
        QVERIFY(remove && remove->isEnabled());
        remove->click();
        QVERIFY(p.links().isEmpty());
        QCOMPARE(table->rowCount(), 0);
        QVERIFY(view.links().isEmpty());
        QCOMPARE(int(edit->extraSelections().size()), extrasWithLink - 1);
        // Undo restores it, in the window and in the view.
        QVERIFY(p.undo());
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(view.links().size(), 1);
    }

    void namesAreNotRichText()
    {
        // Rich text reads the files named by <img src> (a network share would get the user's credentials).
        const QString file = m_dir.filePath("pixel.png");
        QImage image(1, 1, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(file));
        const QString name = QStringLiteral("<img src=\"%1\"> & co").arg(QUrl::fromLocalFile(file).toString());
        QVERIFY(Qt::mightBeRichText(name));

        // Tooltips built from names are rich text that shows the name literally, with no image.
        Project p;
        QVERIFY(p.create(m_dir.filePath("names.tsra")));
        p.addCode(0, name, Qt::red);
        p.addDocument(p.groups().first().id, name, "testo");
        CodeTree codes(&p);
        codes.rebuild();
        DocumentTree docs(&p);
        docs.rebuild();
        for (const QString &tip : {codes.topLevelItem(0)->toolTip(0), docs.topLevelItem(0)->child(0)->toolTip(0)}) {
            QVERIFY(Qt::mightBeRichText(tip));
            QTextDocument doc;
            doc.setHtml(tip);
            QVERIFY2(doc.toPlainText().startsWith(name), qPrintable(doc.toPlainText()));
            QVERIFY(!doc.toPlainText().contains(QChar::ObjectReplacementCharacter));
        }
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

    void activateByVariables()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("varfilter.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", "uno uno");
        const qint64 d2 = p.addDocument(g, "D2", "due due");
        const qint64 d3 = p.addDocument(g, "D3", "tre tre");
        const qint64 sex = p.addVariable("Genere", VariableType::Text);
        const qint64 age = p.addVariable("Età", VariableType::Integer);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d1, age, "45");
        p.setVariableValue(d2, sex, "F");
        p.setVariableValue(d2, age, "30");
        p.setVariableValue(d3, sex, "M");
        p.setVariableValue(d3, age, "50");
        const qint64 code = p.addCode(0, "C", Qt::red);
        p.addSegment(d1, code, 0, 3);
        p.addSegment(d2, code, 0, 3);
        p.addSegment(d3, code, 0, 3);

        DocumentTree tree(&p);
        SegmentsPanel panel(&p);
        panel.setActivation({}, {code});
        QCOMPARE(panel.segments().size(), 3); // no active document = all

        VariableFilterDialog dlg(&p);
        connect(&dlg, &VariableFilterDialog::activationRequested, &tree, &DocumentTree::setActiveDocuments);
        connect(&dlg, &VariableFilterDialog::activationRequested, this,
                [&](const QList<qint64> &ids) { panel.setActivation(ids, {code}); });
        dlg.show();

        using Op = VariableFilter::Op;
        dlg.setConditions({{sex, Op::Equals, "F", {}}, {age, Op::GreaterEq, "40", {}}}, false);
        QCOMPARE(dlg.matchingDocuments(), (QList<qint64>{d1}));
        QPushButton *activate = nullptr;
        for (QPushButton *btn : dlg.findChildren<QPushButton *>())
            if (btn->text() == QStringLiteral("Attiva"))
                activate = btn;
        QVERIFY(activate && activate->isEnabled());
        activate->click();
        QCOMPARE(tree.activeDocumentIds(), (QList<qint64>{d1}));
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().documentId, d1);

        // "Any" widens the selection.
        dlg.setConditions({{sex, Op::Equals, "F", {}}, {age, Op::GreaterEq, "40", {}}}, true);
        QCOMPARE(dlg.matchingDocuments().size(), 3);

        // A condition nobody satisfies cannot be activated (it would mean "all documents").
        dlg.setConditions({{age, Op::Greater, "99", {}}}, false);
        QVERIFY(dlg.matchingDocuments().isEmpty());
        QVERIFY(!activate->isEnabled());

        // The last filter is remembered for the next time the dialog opens.
        dlg.setConditions({{age, Op::Less, "40", {}}}, false);
        activate->click();
        VariableFilterDialog again(&p);
        QCOMPARE(again.conditions().size(), 1);
        QCOMPARE(again.conditions().first().op, Op::Less);
        QCOMPARE(again.conditions().first().value, QString("40"));
        QCOMPARE(again.matchingDocuments(), (QList<qint64>{d2}));
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

    void statsDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("stats.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", QString(40, 'x'));
        const qint64 d2 = p.addDocument(g, "D2", QString(40, 'x'));
        const qint64 sex = p.addVariable("Sesso", VariableType::Text);
        p.setVariableValue(d1, sex, "F");
        p.setVariableValue(d2, sex, "M");
        const qint64 a = p.addCode(0, "A", Qt::red);
        p.addSegment(d1, a, 0, 10);
        p.addSegment(d1, a, 20, 30);
        p.addSegment(d2, a, 0, 10);

        StatsDialog dlg(&p, {}, {});
        dlg.show();
        dlg.selectVariable(sex);
        QCOMPARE(dlg.variableStats().valid, 2);
        QCOMPARE(dlg.variableStats().frequencies.size(), 2);

        QCOMPARE(dlg.chartBars(), 2); // F and M
        QVERIFY(!dlg.chiSquare().valid);
        dlg.setColumns(StatsDialog::Columns::Variable, sex);
        QCOMPARE(dlg.crosstab().columns.size(), 2);
        QCOMPARE(dlg.crosstab().count(0, 0), 2);
        QCOMPARE(dlg.crosstab().count(0, 1), 1);
        dlg.setMeasure(Stats::Measure::Documents);
        QCOMPARE(dlg.crosstab().count(0, 0), 1);
        dlg.setPercent(Stats::Percent::OfColumn);
        shot(&dlg, "stats");
        auto *table = dlg.findChild<QTableWidget *>("crosstabTable");
        QVERIFY(table);
        QCOMPARE(table->item(0, 1)->text(), QString("100.0 %"));
        QCOMPARE(table->item(1, 0)->text(), QString("Totale documenti"));
        QCOMPARE(table->item(1, 1)->text(), QString("1"));
    }

    void summaryGridDialog()
    {
        const QString file = m_dir.filePath("summary-grid.tsra");
        qint64 d1 = 0, d2 = 0, a = 0, b = 0;
        {
            Project p;
            QVERIFY(p.create(file));
            const qint64 g = p.groups().first().id;
            d1 = p.addDocument(g, "D1", "alfa beta gamma delta");
            d2 = p.addDocument(g, "D2", "epsilon zeta");
            a = p.addCode(0, "A", Qt::red);
            b = p.addCode(0, "B", Qt::blue);
            p.addSegment(d1, a, 0, 4);
            p.addSegment(d1, a, 5, 9);
            p.addSegment(d2, b, 0, 7);

            SummaryGridDialog dlg(&p, {}, {});
            dlg.show();
            QCOMPARE(dlg.grid()->rowCount(), 2);
            QCOMPARE(dlg.grid()->columnCount(), 2);
            QCOMPARE(dlg.grid()->item(0, 0)->text(), QString("2")); // D1 x A
            QVERIFY(dlg.grid()->item(0, 1)->text().isEmpty());
            QVERIFY(dlg.grid()->item(0, 0)->icon().isNull());

            dlg.selectCell(d1, a);
            QCOMPARE(dlg.currentDocument(), d1);
            QCOMPARE(dlg.segmentRows(), 2);
            dlg.setEditorText("La sintesi di D1 su A");
            // Switching cell saves.
            dlg.selectCell(d2, b);
            QCOMPARE(p.summary(d1, a).body, QString("La sintesi di D1 su A"));
            QVERIFY(!dlg.grid()->item(0, 0)->icon().isNull()); // marked
            QVERIFY(dlg.editorText().isEmpty());
            // A cell without segments can have a summary too.
            dlg.selectCell(d2, a);
            QCOMPARE(dlg.segmentRows(), 1); // the placeholder line
            dlg.setEditorText("senza segmenti");
            dlg.flush();
            QCOMPARE(p.summary(d2, a).body, QString("senza segmenti"));
            // Back to the first: the text is loaded.
            dlg.selectCell(d1, a);
            QCOMPARE(dlg.editorText(), QString("La sintesi di D1 su A"));
            // Closing saves what is being typed.
            dlg.setEditorText("riscritta");
            dlg.close();
            QCOMPARE(p.summary(d1, a).body, QString("riscritta"));
            shot(&dlg, "summary-grid");
        }
        // Reopen the project and the grid: the summaries are there.
        Project p;
        QVERIFY(p.open(file));
        SummaryGridDialog dlg(&p, {d1}, {a});
        QCOMPARE(dlg.grid()->rowCount(), 1);
        QVERIFY(!dlg.grid()->item(0, 0)->icon().isNull());
        dlg.selectCell(d1, a);
        QCOMPARE(dlg.editorText(), QString("riscritta"));
        // A change made elsewhere (here: undo) refreshes the editor.
        QVERIFY(p.setSummary(d1, a, "esterna"));
        QCOMPARE(dlg.editorText(), QString("esterna"));
        QVERIFY(p.undo());
        QCOMPARE(dlg.editorText(), QString("riscritta"));
    }
    void summaryTablesDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("summary-tables.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", "uno");
        const qint64 d2 = p.addDocument(g, "D2", "due");
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::blue);
        const qint64 age = p.addVariable("Eta", VariableType::Integer);
        p.setVariableValue(d1, age, "30");
        p.setSummary(d1, a, "s1");

        SummaryTablesDialog dlg(&p, {}, {});
        dlg.show();
        QCOMPARE(dlg.currentTableId(), qint64(0));
        const qint64 id = dlg.createTable("Confronto");
        QCOMPARE(dlg.currentTableId(), id);
        QCOMPARE(dlg.currentTable().size(), 3);               // all documents and codes
        QCOMPARE(dlg.currentTable()[0].size(), 3);
        QCOMPARE(dlg.preview()->rowCount(), 2);
        Summaries::TableConfig cfg = dlg.config();
        cfg.codeIds = {a};
        cfg.variableIds = {age};
        dlg.setConfig(cfg);
        QCOMPARE(dlg.currentTable()[0], (QStringList{"Documento", "Eta", "A"}));
        QCOMPARE(dlg.currentTable()[1], (QStringList{"D1", "30", "s1"}));
        // Saved in the project...
        QCOMPARE(Summaries::configFromJson(p.summaryTable(id).configJson).variableIds, QList<qint64>{age});
        // ...and regenerated when a summary changes.
        p.setSummary(d2, a, "s2");
        QCOMPARE(dlg.currentTable()[2][2], QString("s2"));
        QCOMPARE(dlg.preview()->item(1, 2)->text(), QString("s2"));
        Q_UNUSED(b);
        shot(&dlg, "summary-tables");
    }    void dictionaryDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("dictionary.tsra")));
        const qint64 g = p.groups().first().id;
        const qint64 d1 = p.addDocument(g, "D1", "Lavoro da casa. Il lavoro da remoto stanca.");
        const qint64 d2 = p.addDocument(g, "D2", "Ufficio e orari fissi.");
        const qint64 age = p.addVariable("Età", VariableType::Integer);
        p.setVariableValue(d1, age, "40");
        const qint64 dict = p.addDictionary("Lavoro");
        const qint64 places = p.addDictCategory(dict, 0, "Luoghi");
        const qint64 remote = p.addDictCategory(dict, places, "Remoto");
        p.addDictEntry(places, "casa");
        p.addDictEntry(places, "ufficio");
        p.addDictEntry(remote, "da remoto");

        DictionaryDialog dlg(&p, [d2] { return QList<qint64>{d2}; });
        dlg.show();
        QCOMPARE(dlg.currentDictionaryId(), dict);
        auto *tree = dlg.findChild<QTreeWidget *>("dictionaryCategories");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
        QCOMPARE(tree->topLevelItem(0)->text(1), QString("2"));

        // Typing an entry (several separated by commas) adds it to the selected category.
        dlg.selectCategory(remote);
        auto *entries = dlg.findChild<QListWidget *>("dictionaryEntries");
        QVERIFY(entries);
        QCOMPARE(entries->count(), 1);
        auto *edit = dlg.findChild<QLineEdit *>();
        QVERIFY(edit);
        edit->setText("smart working, telelavor*");
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(entries->count(), 3);
        QCOMPARE(p.dictEntries(remote).size(), 3);
        QCOMPARE(p.undoLabel(), QString("Nuova voce del dizionario"));
        QVERIFY(p.undo()); // both entries in one step
        QCOMPARE(entries->count(), 1);
        shot(&dlg, "dictionary-editor");

        dlg.runAnalysis();
        auto *table = dlg.findChild<QTableWidget *>("dictionaryAnalysisTable");
        QVERIFY(table);
        QCOMPARE(table->rowCount(), 3); // two documents and the total
        QCOMPARE(table->columnCount(), 4); // document, words, two categories
        QCOMPARE(table->horizontalHeaderItem(3)->text(), QString("Luoghi > Remoto"));
        QCOMPARE(table->item(0, 1)->text(), QString("8"));
        QCOMPARE(table->item(0, 2)->text(), QString("2")); // casa + da remoto
        QCOMPARE(table->item(0, 3)->text(), QString("1"));
        QCOMPARE(table->item(1, 2)->text(), QString("1"));
        QCOMPARE(table->item(2, 0)->text(), QString("Totale"));
        QCOMPARE(table->item(2, 2)->text(), QString("3"));
        dlg.setValues(DictionaryDialog::Values::PercentOfWords);
        QCOMPARE(table->item(0, 2)->text(), QString("25.00 %"));
        dlg.setWithVariables(true);
        QCOMPARE(table->columnCount(), 5);
        QCOMPARE(table->item(0, 4)->text(), QString("40"));
        QCOMPARE(dlg.analysisTable().first().last(), QString("Età"));
        dlg.findChild<QTabWidget *>()->setCurrentIndex(1);
        shot(&dlg, "dictionary-analysis");

        dlg.setActiveDocumentsOnly(true);
        QCOMPARE(dlg.analysis().documents, QList<qint64>{d2});

        // Automatic coding: one undo step, codes named after the categories.
        dlg.setActiveDocumentsOnly(false);
        QCOMPARE(dlg.autocode({remote}, {}, Dictionaries::Extent::Sentence), 1);
        const qint64 code = p.findCode(p.findCode(p.findCode(0, "Lavoro"), "Luoghi"), "Remoto");
        QVERIFY(code > 0);
        const QList<Segment> segs = p.segments({}, {code});
        QCOMPARE(segs.size(), 1);
        QCOMPARE(p.documentText(d1).mid(segs[0].start, segs[0].length()), QString("Il lavoro da remoto stanca."));
        QVERIFY(p.undo());
        QVERIFY(p.codes().isEmpty());
        QVERIFY(p.allSegments().isEmpty());
    }

    void reportDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("report-ui.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", "Una frase da citare nel report.");
        const qint64 code = p.addCode(0, "Tema", Qt::red);
        p.addSegment(doc, code, 0, 8);

        ReportDialog dlg(&p);
        dlg.show();
        QCOMPARE(dlg.format(), ReportDialog::Format::Docx);
        QVERIFY(dlg.options().codebook);
        QVERIFY(dlg.options().segments);
        Report::Options o = dlg.options();
        o.title = "Mio report";
        o.grouping = Report::Grouping::ByDocument;
        o.examplesPerCode = 3;
        o.variables = true;
        dlg.setOptions(o);
        QCOMPARE(dlg.options().title, QString("Mio report"));
        QCOMPARE(dlg.options().grouping, Report::Grouping::ByDocument);
        QCOMPARE(dlg.options().examplesPerCode, 3);
        QVERIFY(dlg.options().variables);

        const struct { ReportDialog::Format format; const char *file; const char *magic; } cases[] = {
            {ReportDialog::Format::Docx, "ui-report.docx", "PK"},
            {ReportDialog::Format::Pdf, "ui-report.pdf", "%PDF"},
            {ReportDialog::Format::Html, "ui-report.html", "<!DOCTYPE"},
        };
        for (const auto &c : cases) {
            dlg.setFormat(c.format);
            QCOMPARE(dlg.format(), c.format);
            const QString out = m_dir.filePath(c.file);
            QVERIFY(dlg.generate(out));
            QFile f(out);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(f.readAll().startsWith(c.magic));
            f.close();
            const QString shots = qEnvironmentVariable("TESSERA_SHOTS"); // keep the files for a manual look
            if (!shots.isEmpty())
                QFile::copy(out, QDir(shots).filePath(c.file));
        }
    }
    void agreementDialog()
    {
        const QString fileA = m_dir.filePath("agreement-a.tsra"), fileB = m_dir.filePath("agreement-b.tsra");
        Project p;
        p.setAuthor("Anna");
        QVERIFY(p.create(fileA));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", "Una frase di prova per i due codificatori.");
        const qint64 code = p.addCode(0, "Tema", Qt::red);
        p.addSegment(doc, code, 0, 10);
        p.addSegment(doc, code, 20, 30);
        QVERIFY(p.saveCopyAs(fileB));
        p.setAuthor("Bruno");
        p.addSegment(doc, code, 0, 9);

        AgreementDialog dlg(&p, {}, {});
        dlg.show();
        QSignalSpy spy(&dlg, &AgreementDialog::disagreementActivated);
        // Anna (A) and Bruno (B): the frequency differs (2 vs 1) in the one document; existence agrees.
        QCOMPARE(dlg.result().total.units, 1);
        QCOMPARE(dlg.result().total.agreements, 1);
        dlg.setLevel(Agreement::Level::Segments);
        QCOMPARE(dlg.result().total.units, 3);
        QCOMPARE(dlg.result().total.agreements, 2);
        const auto tables = dlg.findChildren<QTableWidget *>();
        QCOMPARE(tables.size(), 2);
        QCOMPARE(tables[0]->rowCount(), 2); // the code and the total
        QCOMPARE(tables[0]->item(1, 0)->text(), QString("Totale"));
        QCOMPARE(tables[1]->rowCount(), 1);
        QVERIFY(tables[1]->item(0, 5)->text().contains("due"));
        emit tables[1]->cellDoubleClicked(0, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last()[0].toLongLong(), doc);
        QCOMPARE(spy.last()[1].toInt(), 20);
        QCOMPARE(spy.last()[2].toInt(), 30);
        QVERIFY(spy.last()[3].toLongLong() != 0);

        // Against another file: Bruno's copy has one more coding.
        {
            Project b;
            b.setAuthor("Carla");
            QVERIFY(b.open(fileB));
            b.addSegment(doc, b.findCode(0, "Tema"), 10, 15);
        }
        QString error;
        QVERIFY2(dlg.setOtherProject(fileB, &error), qPrintable(error));
        dlg.setCoders("Anna", "Carla");
        dlg.setLevel(Agreement::Level::Segments);
        QCOMPARE(dlg.comparedDocuments(), 1);
        QCOMPARE(dlg.result().total.units, 3); // Anna's 2 against Carla's 1
        QCOMPARE(dlg.result().total.agreements, 0);
        QCOMPARE(tables[1]->rowCount(), 3);
        spy.clear();
        for (int r = 0; r < 3; ++r)
            if (tables[1]->item(r, 2)->text() == "Solo B")
                emit tables[1]->cellDoubleClicked(r, 0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.last()[1].toInt(), 10);
        QCOMPARE(spy.last()[3].toLongLong(), qint64(0)); // the segment belongs to the other project
    }

    void cooccurrenceDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("cooccurrence.tsra")));
        const qint64 doc = p.addDocument(p.groups().first().id, "D", QString(40, 'x'));
        const qint64 a = p.addCode(0, "A", Qt::red);
        const qint64 b = p.addCode(0, "B", Qt::blue);
        p.addSegment(doc, a, 0, 10);
        p.addSegment(doc, a, 20, 30);
        p.addSegment(doc, b, 5, 15);

        CooccurrenceDialog dlg(&p, {}, {});
        SegmentsPanel panel(&p);
        connect(&dlg, &CooccurrenceDialog::passagesRequested, &panel, &SegmentsPanel::setPassages);
        dlg.show();

        QCOMPARE(dlg.matrix().frequency(a), 2);
        QCOMPARE(dlg.matrix().count(a, b), 1);
        QTableWidget *table = dlg.findChildren<QTableWidget *>().first();
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->columnCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QString("2")); // diagonal = frequency
        QCOMPARE(table->item(0, 1)->text(), QString("1"));
        QCOMPARE(table->item(1, 0)->text(), QString("1"));

        // Double click on a cell: the segments panel shows the overlapping passage.
        QVERIFY(!panel.hasRetrieval());
        emit table->cellDoubleClicked(0, 1);
        QVERIFY(panel.hasRetrieval());
        QCOMPARE(panel.segments().size(), 1);
        QCOMPARE(panel.segments().first().start, 5);
        QCOMPARE(panel.segments().first().end, 10);
        shot(&dlg, "cooccurrence");
        panel.clearRetrieval();
        QVERIFY(!panel.hasRetrieval());

        // Map: one node per code, edges below the threshold are hidden.
        QCOMPARE(dlg.mapNodeCount(), 2);
        QCOMPARE(dlg.mapEdgeCount(), 1);
        dlg.setMapThreshold(2);
        QCOMPARE(dlg.mapEdgeCount(), 0);
        dlg.setMapThreshold(1);

        // Normalized values and the same-document criterion.
        dlg.setNormalized(true);
        QCOMPARE(table->item(0, 0)->text(), QString("2")); // the diagonal keeps the frequency
        QCOMPARE(table->item(0, 1)->text(), QString("0.50")); // 1 / (2 + 1 - 1)
        dlg.setNormalized(false);
        dlg.setCriterion(Cooccurrence::Criterion::SameDocument);
        QCOMPARE(dlg.matrix().count(a, b), 1);
        QCOMPARE(dlg.matrix().frequency(a), 1);
    }

    void surveyImportDialog()
    {
        Project p;
        QVERIFY(p.create(m_dir.filePath("survey.tsra")));
        Survey::Table table{{"Rispondente", "Genere", "Età", "Cosa ti piace del lavoro da remoto?", "Vuota"}};
        for (int i = 1; i <= 6; ++i)
            table << QStringList{QString("P%1").arg(i), i % 2 ? "F" : "M", QString::number(30 + i),
                                 QString("Risposta numero %1, scritta con calma e qualche dettaglio").arg(i), ""};
        table << QStringList{"", "", "", "", ""}; // an empty row is not a respondent

        SurveyImportDialog dlg(&p, table, "C:/dati/Sondaggio lavoro.xlsx");
        dlg.show();
        QCOMPARE(dlg.respondentCount(), 6);
        Survey::Options o = dlg.options();
        QCOMPARE(o.groupName, QString("Sondaggio lavoro"));
        QVERIFY(o.codeAnswers);
        using R = Survey::Role;
        QCOMPARE(o.columns.size(), 5);
        QCOMPARE(o.columns[0].role, R::Id);
        QCOMPARE(o.columns[1].role, R::Variable);
        QCOMPARE(o.columns[2].role, R::Variable);
        QCOMPARE(o.columns[2].type, VariableType::Integer);
        QCOMPARE(o.columns[3].role, R::OpenAnswer);
        QCOMPARE(o.columns[4].role, R::Ignore);
        shot(&dlg, "survey_import");

        QPushButton *import = nullptr;
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Importa"))
                import = b;
        QVERIFY(import && import->isEnabled());

        // One respondent ID: choosing another column releases the first.
        dlg.setColumnRole(1, R::Id);
        o = dlg.options();
        QCOMPARE(o.columns[0].role, R::Ignore);
        QCOMPARE(o.columns[1].role, R::Id);
        dlg.setColumnRole(0, R::Id);
        dlg.setColumnRole(1, R::Variable);
        dlg.setColumnType(2, VariableType::Text);
        QCOMPARE(dlg.options().columns[2].type, VariableType::Text);
        dlg.setColumnType(2, VariableType::Integer);

        // Without open answers there is nothing to import.
        dlg.setColumnRole(3, R::Ignore);
        QVERIFY(!import->isEnabled());
        dlg.setColumnRole(3, R::OpenAnswer);
        QVERIFY(import->isEnabled());

        Survey::Report report;
        QString error;
        QVERIFY2(Survey::importSurvey(p, table, dlg.options(), &report, &error), qPrintable(error));
        QCOMPARE(report.documents, 6);
        QCOMPARE(p.documentsInGroup(report.groupId).first().name, QString("P1"));
        QCOMPARE(p.codes().size(), 1);
        QCOMPARE(p.variables().size(), 2);

        // Variables already in the project keep their type.
        SurveyImportDialog again(&p, table, "altro.csv");
        QVERIFY(again.options().columns[2].existingVariable);
        auto *columns = again.findChild<QTableWidget *>("surveyColumns");
        QVERIFY(columns);
        auto *ageType = qobject_cast<QComboBox *>(columns->cellWidget(2, 2));
        QVERIFY(ageType && !ageType->isEnabled());
        QCOMPARE(ageType->currentData().toInt(), int(VariableType::Integer));
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

    void memoLinks()
    {
        const qint64 a = codeNamed("Benefici");
        const qint64 b = codeNamed("Isolamento");
        const qint64 c = codeNamed("Strumenti digitali");
        const qint64 doc = m_project->documents().first().id;
        Memo memo;
        memo.targetType = MemoTarget::Free;
        MemoDialog dlg(m_project, memo);
        dlg.show();
        QVERIFY(dlg.links().isEmpty());

        // A code dropped on the text becomes a link, not text.
        auto *body = dlg.findChild<QPlainTextEdit *>();
        QMimeData codeMime;
        codeMime.setData(QString::fromLatin1(Ui::CodeMimeType), QByteArray::number(a));
        codeMime.setText("Benefici");
        const QPointF pos(20, 20);
        QDragEnterEvent enter(pos.toPoint(), Qt::CopyAction, &codeMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(body->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(pos, Qt::CopyAction, &codeMime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(body->viewport(), &drop);
        QVERIFY(body->toPlainText().isEmpty());
        QCOMPARE(dlg.links(), (QList<MemoLink>{{MemoTarget::Code, a}}));

        // Documents dragged from the document tree carry their id.
        DocumentTree docs(m_project);
        docs.rebuild();
        const QModelIndex docIndex = docs.model()->index(0, 0, docs.model()->index(0, 0));
        std::unique_ptr<QMimeData> docMime(docs.model()->mimeData({docIndex}));
        QVERIFY(docMime->hasFormat(QString::fromLatin1(Ui::DocumentMimeType)));
        QDragEnterEvent docEnter(pos.toPoint(), Qt::CopyAction, docMime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&dlg, &docEnter);
        QVERIFY(docEnter.isAccepted());
        QDropEvent docDrop(pos, Qt::CopyAction, docMime.get(), Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&dlg, &docDrop);
        QVERIFY(docDrop.isAccepted());
        QCOMPARE(dlg.links().size(), 2);
        QCOMPARE(dlg.links().last(), (MemoLink{MemoTarget::Document, doc}));

        // The picker lists codes, documents and segments; already linked ones are not added twice.
        MemoLinkPicker picker(m_project, dlg.links());
        int checkedNow = 0;
        for (QTreeWidgetItemIterator it(picker.tree()); *it; ++it) {
            if ((*it)->checkState(0) == Qt::Checked)
                ++checkedNow;
            if ((*it)->text(0) == "Strumenti digitali" || (*it)->text(0) == "Isolamento" ||
                (*it)->text(0) == "Benefici")
                (*it)->setCheckState(0, Qt::Checked);
        }
        QCOMPARE(checkedNow, 2);
        QCOMPARE(picker.selectedLinks(), (QList<MemoLink>{{MemoTarget::Code, b}, {MemoTarget::Code, c}}));
        dlg.addLinks(picker.selectedLinks());
        QCOMPARE(dlg.links().size(), 4);
        dlg.removeLinks({1}); // the document
        dlg.findChild<QLineEdit *>()->setText("Memo teorico");
        shot(&dlg, "memo_links");
        QVERIFY(dlg.save());

        const qint64 id = dlg.memoId();
        QVERIFY(id);
        QCOMPARE(m_project->memo(id).targetType, MemoTarget::Free);
        const QList<MemoLink> abc = {{MemoTarget::Code, a}, {MemoTarget::Code, b}, {MemoTarget::Code, c}};
        QCOMPARE(m_project->memoLinks(id), abc);
        QCOMPARE(m_project->undoLabel(), QString("Memo"));

        // The memo icon appears on all three codes.
        CodeTree codes(m_project);
        codes.rebuild();
        for (const QString &name : {QString("Benefici"), QString("Isolamento"), QString("Strumenti digitali")}) {
            const QList<QTreeWidgetItem *> found = codes.findItems(name, Qt::MatchExactly | Qt::MatchRecursive);
            QCOMPARE(found.size(), 1);
            QVERIFY(!found.first()->icon(1).isNull());
        }

        // The memo manager lists every element of the memo.
        MemoManager memos(m_project);
        auto *table = memos.findChild<QTableWidget *>();
        bool listed = false;
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == "Memo teorico")
                listed = table->item(row, 0)->text().count("Codice:") == 3;
        QVERIFY(listed);

        QVERIFY(m_project->undo());
        QVERIFY(!m_project->memo(id).id);
    }

    void wordFrequencies()
    {
        WordFrequencyDialog dlg(m_project, [] { return QList<qint64>{}; }, [this] { return m_project->allSegments(); });
        dlg.resize(900, 600);
        dlg.setMinLength(3);
        dlg.run();
        QVERIFY(dlg.isBusy());
        QTRY_VERIFY(!dlg.isBusy());
        auto *table = dlg.findChild<QTableWidget *>("wordFrequencyTable");
        auto *kwic = dlg.findChild<QTableWidget *>("keywordTable");
        QVERIFY(table && kwic);
        const Words::Result all = dlg.result();
        QCOMPARE(table->rowCount(), int(all.entries.size()));
        QCOMPARE(all.documents, 3);
        QCOMPARE(table->item(0, 0)->text(), all.entries[0].word);
        QCOMPARE(table->item(0, 1)->data(Qt::DisplayRole).toInt(), all.entries[0].count);
        QVERIFY(all.indexOf("lavoro") >= 0);
        QCOMPARE(all.indexOf("che"), -1); // Italian stop list

        // The stop list can be turned off; sorting by a column reorders the rows.
        dlg.setUseStopList(false);
        dlg.setMinLength(1);
        dlg.run();
        QTRY_VERIFY(!dlg.isBusy());
        QVERIFY(dlg.result().indexOf("che") >= 0);
        QVERIFY(dlg.result().countedWords > all.countedWords);
        table->sortByColumn(0, Qt::AscendingOrder);
        QVERIFY(table->item(0, 0)->text() <= table->item(1, 0)->text());
        dlg.setUseStopList(true);
        dlg.setMinLength(3);

        // Filter by text.
        auto *filter = dlg.findChild<QLineEdit *>("wordFilter");
        dlg.run();
        QTRY_VERIFY(!dlg.isBusy());
        filter->setText("lavor");
        QVERIFY(dlg.visibleRows() > 0 && dlg.visibleRows() < table->rowCount());
        filter->clear();
        QCOMPARE(dlg.visibleRows(), table->rowCount());
        shot(&dlg, "word_frequencies");

        // Cloud: the first N words.
        dlg.setTopWords(20);
        QCOMPARE(dlg.cloudWordCount(), 20);

        // Comparison columns: one per group; the counts add up.
        dlg.setComparison(WordFrequencyDialog::CompareGroups);
        dlg.run();
        QTRY_VERIFY(!dlg.isBusy());
        QCOMPARE(dlg.result().columnLabels.size(), m_project->groups().size());
        QCOMPARE(table->columnCount(), 4 + int(m_project->groups().size()));
        QCOMPARE(dlg.result().entries[0].columns.value(0), dlg.result().entries[0].count);
        dlg.setComparison(WordFrequencyDialog::CompareNone);

        // Words in context: the occurrences open the document at the word.
        QSignalSpy opened(&dlg, &WordFrequencyDialog::occurrenceActivated);
        dlg.run();
        QTRY_VERIFY(!dlg.isBusy());
        dlg.showKeyword("lavoro");
        QVERIFY(!dlg.occurrences().isEmpty());
        QCOMPARE(kwic->rowCount(), int(dlg.occurrences().size()));
        QCOMPARE(kwic->item(0, 2)->text().toLower(), QString("lavoro"));
        kwic->sortByColumn(3, Qt::DescendingOrder); // sorting must not break the link to the occurrence
        emit kwic->cellDoubleClicked(0, 0);
        QCOMPARE(opened.count(), 1);
        const qint64 doc = opened.first()[0].toLongLong();
        const int start = opened.first()[1].toInt(), end = opened.first()[2].toInt();
        QCOMPARE(m_project->documentText(doc).mid(start, end - start).toLower(), QString("lavoro"));
        QCOMPARE(kwic->item(0, 0)->text(), m_project->document(doc).name);

        // Actions on the selected words: search, dictionary and stop list.
        QSignalSpy search(&dlg, &WordFrequencyDialog::searchRequested);
        dlg.selectWords({"lavoro"});
        QCOMPARE(dlg.selectedWords(), QStringList({"lavoro"}));
        for (QPushButton *b : dlg.findChildren<QPushButton *>())
            if (b->text() == "Cerca e codifica…")
                b->click();
        QCOMPARE(search.count(), 1);
        QCOMPARE(search.first()[0].toString(), QString("lavoro"));

        const qint64 category = m_project->dictCategories(m_project->dictionaries().first().id).first().id;
        const qsizetype entries = m_project->dictEntries(category).size();
        QCOMPARE(dlg.addSelectedToDictionary(category), 1);
        QCOMPARE(m_project->dictEntries(category).size(), entries + 1);
        QVERIFY(m_project->undo());
        QCOMPARE(m_project->dictEntries(category).size(), entries);

        dlg.selectWords({"lavoro"});
        dlg.excludeSelectedWords();
        QTRY_VERIFY(!dlg.isBusy());
        QCOMPARE(dlg.result().indexOf("lavoro"), -1);
        QVERIFY(Words::stopList(*m_project).contains("lavoro"));
        Words::setStopList(*m_project, Words::builtInStopWords("it"));

        // Only the retrieved segments.
        dlg.setScope(WordFrequencyDialog::Scope::RetrievedSegments);
        dlg.run();
        QTRY_VERIFY(!dlg.isBusy());
        QVERIFY(dlg.result().totalWords > 0 && dlg.result().totalWords < all.totalWords);

        // "Cerca e codifica" opens the search already filled in.
        SearchDialog searchDialog(m_project, [] { return QList<qint64>{}; }, [] { return qint64(0); });
        searchDialog.searchFor("video");
        QCOMPARE(searchDialog.findChild<QLineEdit *>()->text(), QString("video"));
        QCOMPARE(searchDialog.findChild<QTableWidget *>()->rowCount(), 1); // whole word: not "videochiamata"
    }

    void imageDocumentView()
    {
        Project p; // separate from the example project, whose document counts other tests rely on
        QVERIFY(p.create(m_dir.filePath("images.tsra")));
        QImage img(200, 100, QImage::Format_RGB32);
        img.fill(Qt::white);
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(img.save(&buffer, "PNG"));
        const qint64 group = p.groups().first().id;
        const qint64 doc = p.addImageDocument(group, "Cartello", bytes);
        const qint64 text = p.addDocument(group, "Testo", "Una frase");
        const qint64 tema = p.addCode(0, "Tema", QColor("#d62728"));
        const qint64 altro = p.addCode(0, "Altro", QColor("#1f77b4"));

        DocumentView view(&p);
        view.resize(800, 600);
        view.show();
        QSignalSpy status(&view, &DocumentView::statusMessage);
        view.openDocument(doc);
        QVERIFY(view.isImage());
        ImageView *iv = view.imageView();
        QVERIFY(iv->hasImage());
        QCOMPARE(iv->imageSize(), QSize(200, 100));
        QVERIFY(!view.hasSelection());
        QCOMPARE(view.codeSelection(tema), qint64(0)); // nothing selected
        QCOMPARE(p.segmentsForDocument(doc, true).size(), 0);

        // Dragging a rectangle selects an area (in image pixels), and Ctrl+E-style coding stores it.
        const QPoint from = iv->mapFromScene(QPointF(20, 10));
        const QPoint to = iv->mapFromScene(QPointF(120, 60));
        QTest::mousePress(iv->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(iv->viewport(), (from + to) / 2);
        QTest::mouseMove(iv->viewport(), to);
        QTest::mouseRelease(iv->viewport(), Qt::LeftButton, Qt::NoModifier, to);
        QVERIFY(view.hasSelection());
        QVERIFY(qAbs(iv->selection().left() - 20) <= 2 && qAbs(iv->selection().top() - 10) <= 2);
        QVERIFY(qAbs(iv->selection().width() - 100) <= 3 && qAbs(iv->selection().height() - 50) <= 3);
        const qint64 area = view.codeSelection(tema);
        QVERIFY(area > 0);
        const Segment s = p.segment(area);
        QCOMPARE(s.kind, SegmentKind::Area);
        QCOMPARE(s.page, 0);
        QVERIFY(qAbs(s.x2 - s.x1 - 100) <= 3);
        QCOMPARE(iv->areasAt(QPointF(50, 30)), QList<qint64>{area});
        QVERIFY(iv->areasAt(QPointF(150, 90)).isEmpty());

        // A click on empty space clears the selection; a click on the area selects it and reports its codes.
        QTest::mouseClick(iv->viewport(), Qt::LeftButton, Qt::NoModifier, iv->mapFromScene(QPointF(180, 90)));
        QVERIFY(!view.hasSelection());
        status.clear();
        QTest::mouseClick(iv->viewport(), Qt::LeftButton, Qt::NoModifier, iv->mapFromScene(QPointF(50, 30)));
        QVERIFY(view.hasSelection());
        QCOMPARE(iv->highlightedArea(), area);
        QCOMPARE(iv->selection(), QRectF(s.x1, s.y1, s.x2 - s.x1, s.y2 - s.y1));
        QVERIFY(!status.isEmpty() && status.last().first().toString().contains("Tema"));

        // A comment shows up in the area's tooltip text and in the label.
        QVERIFY(p.setSegmentComment(area, "scritta sbiadita"));
        QCOMPARE(iv->highlightedArea(), area); // the view reloaded and kept the highlight
        QTest::mouseClick(iv->viewport(), Qt::LeftButton, Qt::NoModifier, iv->mapFromScene(QPointF(50, 30)));
        QVERIFY(status.last().first().toString().contains("scritta sbiadita"));

        // Another code dropped on the selected area codes the same rectangle.
        auto *mime = new QMimeData;
        mime->setData(Ui::CodeMimeType, QByteArray::number(altro));
        const QPoint inside = iv->mapFromScene(QPointF(50, 30));
        QDragEnterEvent enter(inside, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QVERIFY(QCoreApplication::sendEvent(iv->viewport(), &enter));
        QVERIFY(enter.isAccepted());
        QDropEvent drop(inside, Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(iv->viewport(), &drop);
        delete mime;
        const QList<Segment> segs = p.segmentsForDocument(doc, true);
        QCOMPARE(segs.size(), 2);
        QCOMPARE(segs[0].x1, segs[1].x1);
        QCOMPARE(iv->areasAt(QPointF(50, 30)).size(), 2);

        // The wheel zooms and stops fitting the window; "Adatta" brings it back.
        QVERIFY(iv->isFitted());
        const double before = iv->zoom();
        QWheelEvent wheel(QPointF(inside), QPointF(iv->viewport()->mapToGlobal(inside)), QPoint(), QPoint(0, 240),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(iv->viewport(), &wheel);
        QVERIFY(iv->zoom() > before);
        QVERIFY(!iv->isFitted());
        iv->fitToWindow();
        QVERIFY(iv->isFitted());

        // Links and text editing do not apply to images.
        view.startLink();
        QVERIFY(!view.hasPendingLink());
        view.setEditing(true);
        QVERIFY(!view.isEditing());

        // The segments panel lists the areas with a thumbnail, and the exports include them.
        SegmentsPanel panel(&p);
        panel.resize(1000, 300);
        panel.setActivation({doc}, {tema});
        QCOMPARE(panel.resultCount(), 1);
        QVERIFY(panel.segments().isEmpty()); // no text to analyse
        const QTableWidget *table = panel.findChild<QTableWidget *>();
        QCOMPARE(table->rowCount(), 1);
        QVERIFY(!table->item(0, 2)->icon().isNull());
        QVERIFY(table->item(0, 2)->text().contains("Area"));
        QCOMPARE(table->item(0, 4)->text(), QString("scritta sbiadita"));
        QTest::qWait(50); // let pending layout requests run before the grab
        shot(&view, "image_document");
        shot(&panel, "image_segments");

        // Opening a text document and coming back keeps the areas; the text view is not affected.
        view.openDocument(text);
        QVERIFY(!view.isImage());
        view.selectRange(0, 3);
        QVERIFY(view.hasSelection());
        view.openDocument(doc);
        QVERIFY(view.isImage());
        QCOMPARE(iv->areasAt(QPointF(50, 30)).size(), 2);
        view.selectRange(0, 0, area); // as when a retrieved segment is activated
        QCOMPARE(iv->highlightedArea(), area);

        // Removing a coding updates the view.
        QVERIFY(p.deleteSegment(segs[1].id));
        QCOMPARE(iv->areasAt(QPointF(50, 30)).size(), 1);
        view.closeDocument();
        QVERIFY(!iv->hasImage());
    }

    void pdfDocumentView()
    {
        Project p; // separate from the example project, whose document counts other tests rely on
        const QString file = m_dir.filePath("pdfview.tsra");
        QVERIFY(p.create(file));
        const QByteArray bytes = makePdf({{"Primo paragrafo sul lavoro", "Secondo paragrafo sulla casa"}, {"Pagina due"}});
        Pdfs::Doc pdf;
        QVERIFY(pdf.load(bytes));
        const qint64 group = p.groups().first().id;
        const qint64 doc = p.addPdfDocument(group, "Rapporto", bytes, Pdfs::joinPages(Pdfs::pageTexts(pdf)));
        const qint64 text = p.addDocument(group, "Testo", "Una frase");
        QVERIFY(doc > 0 && text > 0);
        const qint64 tema = p.addCode(0, "Tema", QColor("#d62728"));
        const qint64 altro = p.addCode(0, "Altro", QColor("#1f77b4"));

        DocumentView view(&p);
        view.resize(900, 700);
        view.show();
        QSignalSpy status(&view, &DocumentView::statusMessage);
        view.openDocument(doc);
        QVERIFY(view.isPdf() && !view.isImage());
        PdfView *pv = view.pdfView();
        QVERIFY(pv->hasPdf());
        QCOMPARE(pv->pageCount(), 2);
        QVERIFY(pv->isFitWidth());
        QVERIFY(pv->zoom() > 0.5 && pv->zoom() < 2);
        QVERIFY(!view.hasSelection());
        QCOMPARE(view.codeSelection(tema), qint64(0)); // nothing selected

        auto drag = [&](int page, const QPointF &a, const QPointF &b, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            const QPoint from = pv->toViewport(page, a), to = pv->toViewport(page, b);
            QTest::mousePress(pv->viewport(), Qt::LeftButton, mods, from);
            QTest::mouseMove(pv->viewport(), (from + to) / 2);
            QTest::mouseMove(pv->viewport(), to);
            QTest::mouseRelease(pv->viewport(), Qt::LeftButton, mods, to);
        };
        auto rectOf = [](const Segment &s) { return QRectF(s.x1, s.y1, s.x2 - s.x1, s.y2 - s.y1); };

        // Dragging over a line selects its text; coding it stores an area with that text.
        drag(1, QPointF(60, 80), QPointF(500, 108));
        QVERIFY(view.hasSelection());
        QVERIFY(pv->selection().isText);
        QVERIFY(pv->selection().text.contains("Primo paragrafo sul lavoro"));
        QVERIFY(!pv->selection().text.contains("Secondo"));
        QVERIFY(view.selectedText().contains("Primo"));
        const qint64 textArea = view.codeSelection(tema);
        QVERIFY(textArea > 0);
        const Segment ts = p.segment(textArea);
        QCOMPARE(ts.kind, SegmentKind::Area);
        QCOMPARE(ts.page, 1);
        QVERIFY(ts.areaText.contains("Primo paragrafo sul lavoro"));
        QVERIFY(ts.x2 > ts.x1 && ts.y2 > ts.y1);

        // Alt + drag selects a rectangle, whatever it covers; the text inside is kept with the coding.
        drag(1, QPointF(60, 135), QPointF(420, 165), Qt::AltModifier);
        QVERIFY(view.hasSelection());
        QVERIFY(!pv->selection().isText);
        QVERIFY(pv->selection().text.contains("Secondo paragrafo"));
        const qint64 rectArea = view.codeSelection(altro);
        QVERIFY(rectArea > 0);
        QVERIFY(p.segment(rectArea).areaText.contains("Secondo paragrafo"));
        QVERIFY(qAbs(rectOf(p.segment(rectArea)).width() - 360) < 1.5 && qAbs(rectOf(p.segment(rectArea)).height() - 30) < 1.5);

        // A click on empty space clears the selection; a click on a coded area selects it and reports its codes.
        QTest::mouseClick(pv->viewport(), Qt::LeftButton, Qt::NoModifier, pv->toViewport(1, QPointF(450, 300)));
        QVERIFY(!view.hasSelection());
        status.clear();
        QTest::mouseClick(pv->viewport(), Qt::LeftButton, Qt::NoModifier, pv->toViewport(1, rectOf(ts).center()));
        QVERIFY(view.hasSelection());
        QCOMPARE(pv->highlightedArea(), textArea);
        QVERIFY(!status.isEmpty() && status.last().first().toString().contains("Tema"));
        QCOMPARE(pv->selection().rect, rectOf(ts));

        // Positions are in points: at any zoom the same codings sit on the same text, and a rectangle drawn
        // at another zoom gives the same coordinates.
        for (double zoom : {0.5, 1.0, 2.25, 4.0}) {
            pv->setZoom(zoom);
            QVERIFY(!pv->isFitWidth());
            QCOMPARE(pv->areasAt(1, rectOf(ts).center()), QList<qint64>{textArea});
            QVERIFY(pv->areasAt(1, QPointF(450, 300)).isEmpty());
            pv->scrollToRect(1, rectOf(ts));
            QTest::mouseClick(pv->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5)); // some other place first
            pv->clearSelection();
            QTest::mouseClick(pv->viewport(), Qt::LeftButton, Qt::NoModifier, pv->toViewport(1, rectOf(ts).center()));
            QCOMPARE(pv->highlightedArea(), textArea);
            QCOMPARE(pv->selection().rect, rectOf(ts));
            pv->clearSelection();
        }
        pv->setZoom(2.0);
        pv->scrollToRect(1, QRectF(60, 135, 360, 30));
        drag(1, QPointF(60, 135), QPointF(420, 165), Qt::AltModifier);
        QVERIFY(qAbs(pv->selection().rect.left() - 60) < 1 && qAbs(pv->selection().rect.width() - 360) < 1.5);
        pv->clearSelection();
        pv->fitWidth();
        QVERIFY(pv->isFitWidth());

        // The code bars of the margin select the area too.
        pv->scrollToRect(1, rectOf(ts));
        QCOMPARE(p.segmentsForDocument(doc, true).size(), 2);

        // Pages: the page box and search hits move the view; the info line tells where we are.
        QSpinBox *pageBox = view.findChild<QSpinBox *>();
        QVERIFY(pageBox && pageBox->isVisible());
        QCOMPARE(pageBox->maximum(), 2);
        pageBox->setValue(2);
        QCOMPARE(pv->currentPage(), 2);
        pv->goToPage(1);
        QCOMPARE(pageBox->value(), 1);
        view.selectRange(int(p.documentText(doc).indexOf("Pagina due")), 0, 0);
        QCOMPARE(pv->currentPage(), 2);
        view.selectRange(0, 0, rectArea); // as when a retrieved segment is activated
        QCOMPARE(pv->currentPage(), 1);
        QCOMPARE(pv->highlightedArea(), rectArea);

        // Links, text editing and the find bar do not apply to PDFs.
        view.startLink();
        QVERIFY(!view.hasPendingLink());
        view.setEditing(true);
        QVERIFY(!view.isEditing());
        view.showFindBar();
        QVERIFY(!view.isFindBarVisible());

        // The segments panel shows the text of an area, or a thumbnail when it has none.
        const qint64 bare = p.addAreaSegment(doc, tema, 2, 60, 80, 300, 110);
        QVERIFY(bare > 0);
        SegmentsPanel panel(&p);
        panel.resize(1000, 300);
        panel.setActivation({doc}, {tema, altro});
        QCOMPARE(panel.resultCount(), 3);
        QVERIFY(panel.segments().isEmpty()); // no text positions to analyse
        const QTableWidget *table = panel.findChild<QTableWidget *>();
        QStringList cells;
        bool thumbnail = false;
        for (int r = 0; r < table->rowCount(); ++r) {
            cells << table->item(r, 2)->text();
            thumbnail = thumbnail || !table->item(r, 2)->icon().isNull();
            QVERIFY(table->item(r, 0)->text().contains("Rapporto · p."));
        }
        QVERIFY(cells.join("|").contains("Primo paragrafo sul lavoro"));
        QVERIFY(cells.join("|").contains("Secondo paragrafo"));
        QVERIFY(thumbnail);
        QTest::qWait(50);
        shot(&view, "pdf_document");
        shot(&panel, "pdf_segments");

        // The lexical search reads the PDF's text.
        SearchDialog search(&p, [] { return QList<qint64>{}; }, [] { return qint64(0); });
        search.searchFor("casa");
        QCOMPARE(search.findChild<QTableWidget *>()->rowCount(), 1);

        // Removing a coding updates the view; other documents are shown as usual.
        QVERIFY(p.deleteSegment(rectArea));
        QCOMPARE(pv->areasAt(1, QPointF(200, 150)).size(), 0);
        view.openDocument(text);
        QVERIFY(!view.isPdf() && !view.isImage());
        QVERIFY(!pv->isVisible());
        view.selectRange(0, 3);
        QVERIFY(view.hasSelection());
        view.openDocument(doc);
        QVERIFY(view.isPdf());
        QCOMPARE(pv->areasAt(1, rectOf(ts).center()).size(), 1);
        view.closeDocument();
        QVERIFY(!pv->hasPdf());

        // Closing and reopening the project: the codings are where they were, at any zoom.
        Project again;
        QVERIFY(again.open(file));
        const Segment back = again.segment(textArea);
        QCOMPARE(back.page, 1);
        QCOMPARE(rectOf(back), rectOf(ts));
        QCOMPARE(back.areaText, ts.areaText);
        DocumentView view2(&again);
        view2.resize(900, 700);
        view2.show();
        view2.openDocument(doc);
        view2.pdfView()->setZoom(1.7);
        QCOMPARE(view2.pdfView()->areasAt(1, rectOf(ts).center()), QList<qint64>{textArea});
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

        // Excel exports: the whole project, and the retrieved segments next to the CSV.
        QAction *exportAll = nullptr;
        for (QAction *a : w.findChildren<QAction *>())
            if (a->text().startsWith("Esporta tutto il progetto in E&xcel"))
                exportAll = a;
        QVERIFY(exportAll && exportAll->isEnabled());
        QStringList buttons;
        for (QToolButton *b : w.findChild<SegmentsPanel *>()->findChildren<QToolButton *>())
            buttons << b->text();
        QVERIFY(buttons.contains("Esporta Excel (.xlsx)"));
        QVERIFY(buttons.contains("Esporta Excel (CSV)"));
    }
    // Installer and icons (#47): ICO files hold 16-256 px entries, the SVG sources render, the
    // Inno Setup script is per-user and associates .tsra, main.cpp opens a project passed as argument.
    void installerAndIcons()
    {
        const QString root = QStringLiteral(TESSERA_SOURCE_DIR);
        for (const char *name : {"tessera.ico", "tessera-file.ico"}) {
            QFile f(root + "/installer/" + QString::fromLatin1(name));
            QVERIFY2(f.open(QIODevice::ReadOnly), name);
            const QByteArray d = f.readAll();
            QCOMPARE(quint8(d[2]), quint8(1)); // type: icon
            const int n = quint8(d[4]);
            QVERIFY(n >= 6);
            QSet<int> sizes;
            for (int i = 0; i < n; ++i) {
                const int w = quint8(d[6 + 16 * i]);
                sizes << (w == 0 ? 256 : w);
                const quint32 size = qFromLittleEndian<quint32>(d.constData() + 6 + 16 * i + 8);
                const quint32 off = qFromLittleEndian<quint32>(d.constData() + 6 + 16 * i + 12);
                QVERIFY(off + size <= quint32(d.size()));
                QVERIFY(!QImage::fromData(d.mid(off, size), "PNG").isNull());
            }
            QVERIFY(sizes.contains(16) && sizes.contains(32) && sizes.contains(48) && sizes.contains(256));
        }
        for (const char *name : {"tessera.svg", "tessera-file.svg"})
            QVERIFY(QSvgRenderer(root + "/installer/" + QString::fromLatin1(name)).isValid());

        QFile iss(root + "/installer/tessera.iss");
        QVERIFY(iss.open(QIODevice::ReadOnly));
        const QString script = QString::fromUtf8(iss.readAll());
        QVERIFY(script.contains("PrivilegesRequired=lowest"));
        QVERIFY(script.contains("ChangesAssociations=yes"));
        QVERIFY(script.contains("Software\\Classes\\.tsra"));
        QVERIFY(script.contains("shell\\open\\command"));
        QVERIFY(script.contains("{autoprograms}"));
        QVERIFY(!script.contains("HKLM")); // per-user: HKA resolves to HKCU

        QFile rc(root + "/src/tessera.rc");
        QVERIFY(rc.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(rc.readAll()).contains("tessera.ico"));

        QFile mainCpp(root + "/src/main.cpp");
        QVERIFY(mainCpp.open(QIODevice::ReadOnly));
        const QString m = QString::fromUtf8(mainCpp.readAll());
        QVERIFY(m.contains("positionalArguments()") && m.contains("openProject"));
        QVERIFY(m.contains("setWindowIcon"));
    }
};

QTEST_MAIN(TestUi)
#include "test_ui.moc"
