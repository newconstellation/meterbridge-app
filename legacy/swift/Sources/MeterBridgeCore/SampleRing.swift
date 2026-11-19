import Synchronization

/// Single-producer, single-consumer ring of audio samples for one channel.
///
/// The audio thread calls `write`; the analysis side calls `read`. There are
/// no locks: the writer publishes how many samples it has ever written with a
/// release store, and the reader checks after copying that the writer has not
/// lapped the region it copied.
public final class SampleRing: @unchecked Sendable {
    public let capacity: Int
    private let mask: Int
    private let storage: UnsafeMutablePointer<Float>
    private let written = Atomic<Int>(0)

    /// `capacity` is rounded up to a power of two.
    public init(capacity requested: Int) {
        var cap = 1
        while cap < requested { cap <<= 1 }
        capacity = cap
        mask = cap - 1
        storage = .allocate(capacity: cap)
        storage.initialize(repeating: 0, count: cap)
    }

    deinit { storage.deallocate() }

    /// Total samples ever written (monotonic). Safe from any thread.
    public var totalWritten: Int { written.load(ordering: .acquiring) }

    /// Audio thread only. Never blocks or allocates.
    public func write(_ source: UnsafePointer<Float>, count: Int) {
        let start = written.load(ordering: .relaxed)
        for i in 0..<count { storage[(start &+ i) & mask] = source[i] }
        written.store(start &+ count, ordering: .releasing)
    }

    /// Copies samples with absolute positions `from ..< from + count` into
    /// `destination`. Returns false if they are not written yet or have
    /// already been overwritten.
    @discardableResult
    public func read(from: Int, count: Int, into destination: UnsafeMutablePointer<Float>) -> Bool {
        let end = written.load(ordering: .acquiring)
        guard count >= 0, from >= 0, from + count <= end, end - from <= capacity else { return false }
        for i in 0..<count { destination[i] = storage[(from + i) & mask] }
        // If the writer moved past our start by more than a lap while we
        // copied, part of what we read is newer audio. Treat it as lost.
        return written.load(ordering: .acquiring) - from <= capacity
    }

    /// Copies the most recent `count` samples. Returns false if not enough yet.
    @discardableResult
    public func readLatest(count: Int, into destination: UnsafeMutablePointer<Float>) -> Bool {
        let end = totalWritten
        return read(from: end - count, count: count, into: destination)
    }
}
