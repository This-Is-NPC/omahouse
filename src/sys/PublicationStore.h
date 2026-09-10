#pragma once
#include "Fleet.h"
#include "Publication.h"
namespace omahouse
{
bool readCollected(const QString& machine, const QString& user, Collected* out, QString* error);
bool readPublished(
    const QString& machine, const QString& user, Published* out, bool* missing, QString* error);
bool publicationRows(
    const Profile& draft, const QVector<Machine>& fleet, QVector<PublicationRow>* rows, QString* error);
bool fileCollected(
    const QString& machine, const QString& user, const QByteArray& raw, QString* error);
bool resolveCollected(const QString& machine, const QString& user, const Collected& collected,
    const Profile& draft, QString* error);
}
