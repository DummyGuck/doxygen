#ifndef MYDIRECTORY_H
#define MYDIRECTORY_H

#include <qcstring.h>
#include <qdir.h>

class MyDir
{
public:
    MyDir( QCString dirPath );

    QString       absolutePath() const;
    QString       path() const { return dir.path(); }
    bool          exists() const { return dir.exists(); }
    QFileInfoList entryInfoList() const { return dir.entryInfoList(); }

private:
    QDir dir;
};

#endif // MYDIRECTORY_H
