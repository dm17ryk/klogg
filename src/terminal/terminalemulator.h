#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>

namespace terminal {
struct Size {
    int columns = 80;
    int rows = 24;
};
enum class Status { Ok, Yielded, InvalidRequest, WrongThread, StaleRevision, ResourceLimit };
struct Result {
    Status status = Status::Ok;
    int consumedBytes = 0;
    QString reason;
};
struct Cell {
    QString text;
    int width = 1;
    quint32 foreground = 0;
    quint32 background = 0;
    bool bold = false;
    bool italic = false;
    bool reverse = false;
    bool conceal = false;
    int underline = 0;
    bool capacityReached = false;
};
// Coordinates are terminal cells; they are never physical source locations.
struct Snapshot {
    Result result;
    quint64 revision = 0;
    Size size;
    int cursorRow = 0;
    int cursorColumn = 0;
    bool cursorVisible = true;
    bool alternateScreen = false;
    int firstCell = 0;
    int nextCell = -1;
    QVector<Cell> cells;
};
enum class Key { Enter, Backspace, Tab, Escape, Up, Down, Left, Right, Home, End, Delete };
struct EncodedInput {
    Result result;
    QByteArray bytes;
};

class TerminalEmulator final {
public:
    static constexpr int MaxColumns = 1000;
    static constexpr int MaxRows = 500;
    static constexpr int MaxInputBytes = 65536;
    static constexpr int MaxReplyBytes = 65536;
    static constexpr int MaxPageCells = 16384;
    // A caller schedules the unconsumed suffix when Yielded is returned.
    static constexpr int InputQuantumBytes = 256;
    explicit TerminalEmulator( Size size = {} );
    ~TerminalEmulator();
    TerminalEmulator( const TerminalEmulator& ) = delete;
    TerminalEmulator& operator=( const TerminalEmulator& ) = delete;
    Result feed( const QByteArray& bytes );
    Result resize( Size size );
    Result reset( Size size );
    Snapshot snapshot( int firstCell = 0, int count = 200, quint64 expectedRevision = 0 ) const;
    QByteArray takeReplies();
    EncodedInput encodeKey( Key key, bool shift = false, bool control = false, bool alt = false );
    EncodedInput encodePaste( const QString& text );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace terminal
