#ifndef QLAUE_CIFREADER_H
#define QLAUE_CIFREADER_H

#include <QString>

class Crystal;

// Replace the destination only after the complete structure has been validated.
bool readCifFile(const QString &filename, Crystal &crystal,
                 QString &error, int &errorLine);

#endif
