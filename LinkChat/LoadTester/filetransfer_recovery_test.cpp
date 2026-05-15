#include "../Client/filetransfersession.h"

#include <QCoreApplication>
#include <QDebug>
#include <cstdio>

namespace {
bool expect(bool value, const char *message)
{
    if (!value) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app)

    bool ok = true;

    FileTransferSession session;
    session.reset(10, QSet<int>{0, 1, 2});

    QList<int> firstWindow = session.takeSendWindow(3);
    ok &= expect(firstWindow == QList<int>({3, 4, 5}), "resume should send first missing chunks");

    const qint64 startMs = 1000;
    for (int chunk : firstWindow) {
        session.markSent(chunk, startMs);
    }

    ok &= expect(session.markAcked(3), "ACK for chunk 3 should be accepted");
    ok &= expect(!session.markAcked(3), "duplicate ACK should be ignored");
    ok &= expect(!session.markAcked(10), "out-of-range ACK should be ignored");

    QList<int> nextWindow = session.takeSendWindow(3);
    ok &= expect(nextWindow == QList<int>({6}), "window should top up one missing slot");
    session.markSent(6, startMs + 10);

    QList<int> timedOut = session.timedOutChunks(startMs + 4000, 3000, 5);
    ok &= expect(timedOut.contains(4) && timedOut.contains(5), "unacked chunks should time out");

    for (int chunk : QList<int>({4, 5, 6, 7, 8, 9})) {
        if (!session.isAcked(chunk)) {
            session.markAcked(chunk);
        }
    }

    ok &= expect(session.isComplete(), "session should complete when all chunks are ACKed");
    ok &= expect(session.state() == FileTransferSession::State::Completed, "state should be Completed");

    if (ok) {
        std::fprintf(stdout, "FileTransferRecoveryTest passed\n");
        return 0;
    }
    return 1;
}
