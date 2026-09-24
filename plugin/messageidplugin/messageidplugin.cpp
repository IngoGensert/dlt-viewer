/**
 * Message ID Mode Plugin for COVESA DLT Viewer 2.28.1
 *
 * Based on the COVESA Non Verbose Mode Plugin.
 *
 * Custom behaviour:
 *  - FIBEX frame selection ONLY by received Message ID
 *  - Payload decoding starts at byte 9
 *  - Received Context ID = payload byte 8, bits 0..3
 *  - Expected Context ID is read from FIBEX CONTEXT_DESCRIPTION
 *    (e.g. "11-MISC" -> 11)
 *  - Expected App ID is read from the FIBEX frame.
 *  - Received App ID is read from the DLT trace message.
 *  - A warning argument is appended after the translated message when
 *    Context ID and/or App ID do not match.
 */

#include <QtGui>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QProgressDialog>
#include <QXmlStreamReader>

#include "messageidplugin.h"
#include "dlt_protocol.h"
#include "dlt_user.h"

extern char *message_type[];
extern const char *log_info[];
extern const char *trace_type[];
extern const char *nw_trace_type[];
extern const char *control_type[];
extern const char *service_id[];
extern const char *return_type[];

MessageIdPlugin::MessageIdPlugin()
{
    dltControl = 0;
}

QString MessageIdPlugin::name()
{
    return QString(MESSAGE_ID_PLUGIN_NAME);
}

QString MessageIdPlugin::pluginVersion()
{
    return MESSAGE_ID_PLUGIN_VERSION;
}

QString MessageIdPlugin::pluginInterfaceVersion()
{
    return PLUGIN_INTERFACE_VERSION;
}

QString MessageIdPlugin::description()
{
    return QString();
}

QString MessageIdPlugin::error()
{
    return m_error_string;
}

bool MessageIdPlugin::loadConfig(QString filename)
{
    m_error_string.clear();
    clear();

    if (filename.isEmpty())
        return true;

    QDir dir(filename);
    if (dir.exists())
    {
        dir.setFilter(QDir::Files);
        QStringList filters;
        filters << "*.xml" << "*.XML";
        dir.setNameFilters(filters);

        QFileInfoList list = dir.entryInfoList();
        for (int i = 0; i < list.size(); ++i)
        {
            QFileInfo fileInfo = list.at(i);
            if (!parseFile(fileInfo.filePath()))
            {
                m_error_string = fileInfo.fileName() + ":\n" + m_error_string;
                return false;
            }
        }
        return true;
    }

    return parseFile(filename);
}

void MessageIdPlugin::clear()
{
    foreach (DltFibexPdu *pdu, pdumap)
        delete pdu;
    pdumap.clear();

    foreach (DltFibexFrame *frame, framemapwithkey)
        delete frame;

    framemapwithkey.clear();
    framemap.clear();
    contextIdMap.clear();
}

bool MessageIdPlugin::parseFile(QString filename)
{
    bool ret = true;

    QFile file(filename);
    if (!file.open(QFile::ReadOnly | QFile::Text))
    {
        m_error_string = "Could not open File: ";
        m_error_string.append(filename).append(" for configuration.");
        return false;
    }

    QString warning_text;

    DltFibexPdu *pdu = 0;
    DltFibexFrame *frame = 0;

    // Used while reading the global FIBEX context definitions.
    bool insideContextDefinition = false;
    QString currentContextName;

    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Start loading Fibex XML " << filename;

    QXmlStreamReader xml(&file);
    QProgressDialog progress("Load Fibex file " + filename,
                             "Abort Load",
                             0,
                             xml.device()->size(),
                             0);

    if (dltControl && !dltControl->silentmode)
    {
        progress.setWindowModality(Qt::WindowModal);
        progress.setWindowTitle(name());
        progress.raise();
        progress.activateWindow();
    }

    int progressCounter = 0;

    while (!xml.atEnd())
    {
        xml.readNext();
        progressCounter++;

        if (dltControl && !dltControl->silentmode && ((progressCounter % 1000) == 0))
        {
            progress.setValue(xml.device()->pos());

            if (progress.wasCanceled())
                break;
        }

        if (xml.isStartElement())
        {
            if (xml.name() == QString("CONTEXT"))
            {
                // Global context definition, not a FRAME context reference.
                if (!frame)
                {
                    insideContextDefinition = true;
                    currentContextName.clear();
                }
            }

            if (xml.name() == QString("PDU"))
            {
                if (!pdu)
                {
                    pdu = new DltFibexPdu();
                    pdu->id = xml.attributes().value(QString("ID")).toString();
                }
            }

            if (xml.name() == QString("DESC"))
            {
                if (pdu)
                    pdu->description = xml.readElementText();
            }

            if (xml.name() == QString("BYTE-LENGTH"))
            {
                if (frame)
                    frame->byteLength = xml.readElementText().toInt();

                if (pdu)
                    pdu->byteLength = xml.readElementText().toInt();
            }

            if (xml.name() == QString("SIGNAL-INSTANCE"))
            {
                // Nothing to do.
            }

            if (xml.name() == QString("SIGNAL-REF"))
            {
                if (pdu)
                {
                    QString text = xml.attributes().value("ID-REF").toString();

                    if (text == "S_BOOL")
                        pdu->typeInfo = QDltArgument::DltTypeInfoBool;
                    else if (text == "S_SINT8")
                        pdu->typeInfo = QDltArgument::DltTypeInfoSInt;
                    else if (text == "S_UINT8")
                        pdu->typeInfo = QDltArgument::DltTypeInfoUInt;
                    else if (text == "S_SINT16")
                        pdu->typeInfo = QDltArgument::DltTypeInfoSInt;
                    else if (text == "S_UINT16")
                        pdu->typeInfo = QDltArgument::DltTypeInfoUInt;
                    else if (text == "S_SINT32")
                        pdu->typeInfo = QDltArgument::DltTypeInfoSInt;
                    else if (text == "S_UINT32")
                        pdu->typeInfo = QDltArgument::DltTypeInfoUInt;
                    else if (text == "S_SINT64")
                        pdu->typeInfo = QDltArgument::DltTypeInfoSInt;
                    else if (text == "S_UINT64")
                        pdu->typeInfo = QDltArgument::DltTypeInfoUInt;
                    else if (text == "S_FLOA16")
                        pdu->typeInfo = QDltArgument::DltTypeInfoFloa;
                    else if (text == "S_FLOA32")
                        pdu->typeInfo = QDltArgument::DltTypeInfoFloa;
                    else if (text == "S_FLOA64")
                        pdu->typeInfo = QDltArgument::DltTypeInfoFloa;
                    else if (text == "S_STRG_ASCII")
                        pdu->typeInfo = QDltArgument::DltTypeInfoStrg;
                    else if (text == "S_STRG_UTF8")
                        pdu->typeInfo = QDltArgument::DltTypeInfoUtf8;
                    else if (text == "S_RAWD" || text == "S_RAW")
                        pdu->typeInfo = QDltArgument::DltTypeInfoRawd;
                    else
                        pdu->typeInfo = 0;
                }
            }

            if (xml.name() == QString("FRAME"))
            {
                if (!frame)
                {
                    frame = new DltFibexFrame();
                    frame->id = xml.attributes().value(QString("ID")).toString();
                    frame->filename = filename;
                }
            }

            if (xml.name() == QString("MANUFACTURER-EXTENSION"))
            {
                // Nothing to do.
            }

            if (xml.name() == QString("MESSAGE_TYPE"))
            {
                if (frame)
                {
                    QString text = xml.readElementText();

                    if (text == QString("DLT_TYPE_LOG"))
                        frame->messageType = DLT_TYPE_LOG;
                    else if (text == QString("DLT_TYPE_APP_TRACE"))
                        frame->messageType = DLT_TYPE_APP_TRACE;
                    else if (text == QString("DLT_TYPE_NW_TRACE"))
                        frame->messageType = DLT_TYPE_NW_TRACE;
                    else if (text == QString("DLT_TYPE_CONTROL"))
                        frame->messageType = DLT_TYPE_CONTROL;
                    else
                        frame->messageType = 0;
                }
            }

            if (xml.name() == QString("MESSAGE_INFO"))
            {
                if (frame)
                {
                    QString text = xml.readElementText();

                    if (text == QString("DLT_LOG_DEFAULT"))
                        frame->messageInfo = DLT_LOG_DEFAULT;
                    else if (text == QString("DLT_LOG_OFF"))
                        frame->messageInfo = DLT_LOG_OFF;
                    else if (text == QString("DLT_LOG_FATAL"))
                        frame->messageInfo = DLT_LOG_FATAL;
                    else if (text == QString("DLT_LOG_ERROR"))
                        frame->messageInfo = DLT_LOG_ERROR;
                    else if (text == QString("DLT_LOG_WARN"))
                        frame->messageInfo = DLT_LOG_WARN;
                    else if (text == QString("DLT_LOG_INFO"))
                        frame->messageInfo = DLT_LOG_INFO;
                    else if (text == QString("DLT_LOG_DEBUG"))
                        frame->messageInfo = DLT_LOG_DEBUG;
                    else if (text == QString("DLT_LOG_VERBOSE"))
                        frame->messageInfo = DLT_LOG_VERBOSE;
                    else
                        frame->messageInfo = 0;
                }
            }

            if (xml.name() == QString("APPLICATION_ID"))
            {
                if (frame)
                    frame->appid = xml.readElementText();
            }

            if (xml.name() == QString("CONTEXT_ID"))
            {
                QString text = xml.readElementText();

                if (frame)
                {
                    // Context name assigned to the frame, e.g. "MISC".
                    frame->ctid = text;
                }
                else if (insideContextDefinition)
                {
                    // Global context definition, e.g. "MISC".
                    currentContextName = text;
                }
            }

            if (xml.name() == QString("CONTEXT_DESCRIPTION"))
            {
                QString description = xml.readElementText();

                if (insideContextDefinition && !currentContextName.isEmpty())
                {
                    // Expected generator format:
                    //   CONTEXT_ID          = MISC
                    //   CONTEXT_DESCRIPTION = 11-MISC
                    //
                    // Extract the numeric part before the first '-'.
                    QString numberText = description.section('-', 0, 0).trimmed();
                    bool ok = false;
                    int numericContextId = numberText.toInt(&ok);

                    if (ok)
                    {
                        contextIdMap[currentContextName] = numericContextId;
                        qDebug() << MESSAGE_ID_PLUGIN_NAME
                                 << ": Context mapping"
                                 << currentContextName
                                 << "->"
                                 << numericContextId;
                    }
                }
            }

            if (xml.name() == QString("PDU-INSTANCE"))
            {
                // Nothing to do.
            }

            if (xml.name() == QString("PDU-REF"))
            {
                if (frame)
                {
                    DltFibexPduRef *ref = new DltFibexPduRef();
                    ref->id = xml.attributes().value(QString("ID-REF")).toString();
                    frame->pdureflist.append(ref);
                    frame->pduRefCounter++;
                }
            }
        }

        if (xml.isEndElement())
        {
            if (xml.name() == QString("CONTEXT"))
            {
                insideContextDefinition = false;
                currentContextName.clear();
            }

            if (xml.name() == QString("PDU"))
            {
                if (pdu)
                {
                    pdumap[pdu->id] = pdu;
                    pdu = 0;
                }
            }

            if (xml.name() == QString("FRAME"))
            {
                if (frame)
                {
                    // Resolve expected numeric Context ID from the global map.
                    frame->contextId = contextIdMap.value(frame->ctid, -1);

                    if (framemap.contains(frame->id))
                    {
                        if (framemapwithkey.contains(
                                DltFibexKey(frame->id, frame->appid, frame->ctid)))
                        {
                            warning_text += frame->id + ", ";
                            delete frame;
                            frame = 0;
                        }
                        else
                        {
                            framemapwithkey[
                                DltFibexKey(frame->id, frame->appid, frame->ctid)] = frame;
                        }
                    }
                    else
                    {
                        framemapwithkey[
                            DltFibexKey(frame->id, frame->appid, frame->ctid)] = frame;
                        framemap[frame->id] = frame;
                    }

                    frame = 0;
                }
            }
        }
    }

    if (xml.hasError())
    {
        m_error_string.append("\nXML Parser error: ")
            .append(xml.errorString())
            .append("\n");
        ret = false;
    }

    file.close();

    // Update all frames once more. This also covers FIBEX files in which the
    // context definitions appear after the frame definitions.
    foreach (DltFibexFrame *storedFrame, framemapwithkey)
    {
        storedFrame->contextId = contextIdMap.value(storedFrame->ctid, -1);
    }

    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Finish loading Fibex XML.";

    if (warning_text.length())
    {
        warning_text.chop(2);
        m_error_string.append("Duplicated FRAMES ignored: \n").append(warning_text);
        ret = true;
    }

    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Start Creating Links";

    foreach (DltFibexFrame *storedFrame, framemapwithkey)
    {
        if (!storedFrame->filename.compare(filename))
        {
            foreach (DltFibexPduRef *ref, storedFrame->pdureflist)
            {
                QHash<QString, DltFibexPdu*>::iterator i = pdumap.find(ref->id);

                while (i != pdumap.end() && i.key() == ref->id)
                {
                    ref->ref = i.value();
                    ++i;
                }
            }
        }
    }

    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Finish Creating Links";

    // Keep original Non-Verbose plugin behaviour.
    pdumap.clear();

    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Size of framemapwithkey"
             << framemapwithkey.size();
    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Size of framemap"
             << framemap.size();
    qDebug() << MESSAGE_ID_PLUGIN_NAME << ": Size of contextIdMap"
             << contextIdMap.size();

    return ret;
}

bool MessageIdPlugin::saveConfig(QString /*filename*/)
{
    return true;
}

QStringList MessageIdPlugin::infoConfig()
{
    QStringList list;

    foreach (DltFibexFrame *frame, framemapwithkey)
    {
        QString text;

        text += frame->id
                + QString(" AppI:%1 CtI:%2 CID:%3 Len:%4 MT:%5 MI:%6")
                      .arg(frame->appid)
                      .arg(frame->ctid)
                      .arg(frame->contextId)
                      .arg(frame->byteLength)
                      .arg(frame->messageType)
                      .arg(frame->messageInfo);

        list.append(text);
    }

    return list;
}

bool MessageIdPlugin::isMsg(QDltMsg &msg, int triggeredByUser)
{
    Q_UNUSED(triggeredByUser)

    // IMPORTANT:
    // Translation decision is based ONLY on the received Message ID.
    QString idtext = QString("ID_%1").arg(msg.getMessageId());

    return framemap.contains(idtext);
}

bool MessageIdPlugin::decodeMsg(QDltMsg &msg, int triggeredByUser)
{
    Q_UNUSED(triggeredByUser)

    // Custom trace payload format:
    // Byte 0..3 : Timestamp
    // Byte 4..7 : Message ID
    // Byte 8    : LL (bits 4..7) + CID (bits 0..3)
    // Byte 9..  : FIBEX user data
    int offset = 9;

    QString idtext = QString("ID_%1").arg(msg.getMessageId());

    // IMPORTANT:
    // Select FIBEX frame ONLY by received Message ID.
    DltFibexFrame *frame = framemap.value(idtext, 0);

    if (!frame)
        return false;

    QByteArray payload = msg.getPayload();

    // Read the received Context ID from byte 8, bits 0..3.
    int receivedContextId = -1;

    if (payload.size() > 8)
    {
        quint8 headerByte = static_cast<quint8>(
            static_cast<unsigned char>(payload.at(8)));

        receivedContextId = headerByte & 0x0F;
    }

    int expectedContextId = frame->contextId;

    // Read App IDs before the normal decoding may fill an empty APID.
    QString expectedAppId = frame->appid;
    QString receivedAppId = msg.getApid();

    /* Set message data */
    if (msg.getApid().isEmpty())
        msg.setApid(frame->appid);

    if (msg.getCtid().isEmpty())
        msg.setCtid(frame->ctid);

    msg.setNumberOfArguments(frame->pdureflist.size());
    msg.setType((QDltMsg::DltTypeDef)(frame->messageType));
    msg.setSubtype(frame->messageInfo);

    /* Look for all PDUs for this message */
    for (int i = 0; i < frame->pdureflist.size(); i++)
    {
        QDltArgument argument;
        QByteArray data;
        unsigned short length;

        DltFibexPdu *pdu = frame->pdureflist[i]->ref;

        if (pdu)
        {
            if (!pdu->description.isEmpty())
            {
                argument.setTypeInfo(QDltArgument::DltTypeInfoStrg);
                argument.setEndianness(msg.getEndianness());
                argument.setOffsetPayload(offset);

                data.append(pdu->description.toUtf8());
                argument.setData(data);
            }
            else
            {
                argument.setTypeInfo(
                    (QDltArgument::DltTypeInfoDef)(pdu->typeInfo));
                argument.setEndianness(msg.getEndianness());
                argument.setOffsetPayload(offset);

                if ((pdu->typeInfo == QDltArgument::DltTypeInfoStrg)
                    || (pdu->typeInfo == QDltArgument::DltTypeInfoRawd)
                    || (pdu->typeInfo == QDltArgument::DltTypeInfoUtf8))
                {
                    if ((unsigned int)payload.size()
                        < (offset + sizeof(unsigned short)))
                        break;

                    if (argument.getEndianness()
                        == QDlt::DltEndiannessLittleEndian)
                    {
                        length = *((unsigned short*)
                                   (payload.constData() + offset));
                    }
                    else
                    {
                        length = DLT_SWAP_16(
                            *((unsigned short*)
                              (payload.constData() + offset)));
                    }

                    offset += sizeof(unsigned short);
                    argument.setData(payload.mid(offset, length));
                    offset += length;
                }
                else
                {
                    argument.setData(
                        payload.mid(offset, pdu->byteLength));
                    offset += pdu->byteLength;
                }
            }

            msg.addArgument(argument);
        }
    }

    // ---------------------------------------------------------------------
    // Context ID / App ID validation
    // ---------------------------------------------------------------------

    bool contextMismatch =
        (expectedContextId >= 0)
        && (receivedContextId >= 0)
        && (expectedContextId != receivedContextId);

    bool appIdMismatch =
        !expectedAppId.isEmpty()
        && (expectedAppId != receivedAppId);

    QString warning;

    if (contextMismatch && appIdMismatch)
    {
        warning = QString(
            " ... [WARNING: exp.CID: %1 rec.CID: %2 / exp.Apid: %3 rec.Apid: %4]")
                      .arg(expectedContextId)
                      .arg(receivedContextId)
                      .arg(expectedAppId)
                      .arg(receivedAppId);
    }
    else if (contextMismatch)
    {
        warning = QString(
            " ... [WARNING: exp.CID: %1 rec.CID: %2]")
                      .arg(expectedContextId)
                      .arg(receivedContextId);
    }
    else if (appIdMismatch)
    {
        warning = QString(
            " ... [WARNING: exp.Apid: %1 rec.Apid: %2]")
                      .arg(expectedAppId)
                      .arg(receivedAppId);
    }

    if (!warning.isEmpty())
    {
        QDltArgument warningArgument;
        warningArgument.setTypeInfo(QDltArgument::DltTypeInfoStrg);
        warningArgument.setEndianness(msg.getEndianness());
        warningArgument.setOffsetPayload(payload.size());
        warningArgument.setData(warning.toUtf8());

        msg.addArgument(warningArgument);

        // One additional visible argument for the warning.
        msg.setNumberOfArguments(frame->pdureflist.size() + 1);
    }

    return true;
}

bool MessageIdPlugin::initControl(QDltControl *control)
{
    dltControl = control;
    return true;
}

bool MessageIdPlugin::initConnections(QStringList)
{
    return false;
}

bool MessageIdPlugin::controlMsg(int, QDltMsg &)
{
    return false;
}

bool MessageIdPlugin::stateChanged(
    int index,
    QDltConnection::QDltConnectionState connectionState,
    QString hostname)
{
    Q_UNUSED(index);
    Q_UNUSED(connectionState);
    Q_UNUSED(hostname);

    return false;
}

bool MessageIdPlugin::autoscrollStateChanged(bool enabled)
{
    Q_UNUSED(enabled);
    return false;
}

void MessageIdPlugin::initMessageDecoder(
    QDltMessageDecoder* pMessageDecoder)
{
    Q_UNUSED(pMessageDecoder);
}

void MessageIdPlugin::initMainTableView(QTableView* pTableView)
{
    Q_UNUSED(pTableView);
}

void MessageIdPlugin::configurationChanged()
{
}

#if QT_VERSION < QT_VERSION_CHECK(5, 0, 0)
Q_EXPORT_PLUGIN2(messageidplugin, MessageIdPlugin);
#endif
