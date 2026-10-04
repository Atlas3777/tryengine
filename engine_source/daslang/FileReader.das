options gen2

module FileReader shared public

require tryAsyncFile public
require daslib/async_boost

def finalize(var file_handle : FileHandle&) {
    finalize_file_handle(file_handle)
}

[async]
def ReadFile(path : string; offset : uint64; size : uint) : array<uint8> {
    var handle <- ReadChunkAsync(path, offset, size)
    while (!handle.IsReady) {
        await_next_frame()
    }
    yield <- GetDataSpan(handle)
}

[async]
def GetFileSize(path : string) : uint64 {
    var handle <- GetStatAsync(path)
    while (!handle.IsReady) {
        await_next_frame()
    }
    let size = handle.GetFileSize
    delete handle
    yield size
}