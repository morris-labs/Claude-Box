#pragma once

#include <QList>
#include <QString>

// A project is a directory that one or more boxes (conversations) work in.
// Projects are implicit: a directory is a project as soon as a box points at
// it, so existing installs need no migration. A ProjectRecord only holds what
// the user or the app has added on top, under ~/.claude-box/projects/<id>.
//
// `root` is the key. Two boxes whose `target_dir` is the same directory
// (ignoring a trailing slash) belong to one project.
struct ProjectRecord {
    QString id;          // filename; derived from root, see idForRoot()
    QString root;        // absolute path of the project directory
    QString displayName; // empty = use the directory's own name
    // The git repository that conversations get worktrees from. Empty when
    // the root is not a repository and none has been chosen.
    QString primaryRepo;

    bool isValid() const { return !id.isEmpty() && !root.isEmpty(); }
    QString name() const;

    static QString projectsDir();
    // Normalised form used to compare and group directories.
    static QString normalizeRoot(const QString &dir);
    static QString idForRoot(const QString &root);

    // The saved record for `root`, or a default one (not yet on disk) when
    // there is none. Always valid for a non-empty root.
    static ProjectRecord forRoot(const QString &root);
    static QList<ProjectRecord> loadAll();
    bool save() const;

    // The root itself when it is a git working tree, otherwise empty.
    static QString detectPrimaryRepo(const QString &root);
};
