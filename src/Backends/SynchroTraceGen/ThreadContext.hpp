#ifndef STGEN_THREADCONTEXT_H
#define STGEN_THREADCONTEXT_H

#include "STEvent.hpp"
#include "TextLogger.hpp"
#include "STTypes.hpp"

/* DynamoRIO sometimes reports very high addresses.
 * For now, allow these addresses until we figure
 * out what to do with them */
#define ALLOW_ADDRESS_OVERFLOW 1
#include "STShadowMemory.hpp"

/* XXX overflow builtin not in GCC <5.
 * This overflow check should only be used for
 * variables which increment by 1 each time. */
#if __GNUC__ >= 5
#define INCR_EID_OVERFLOW(var) __builtin_add_overflow(var, 1, &var)
#else
#define INCR_EID_OVERFLOW(var) (var == UINT_MAX ? true : (var += 1) && false)
#endif

namespace STGen
{

template <class LoggerStrategy>
class ThreadContext
{
  public:
    ThreadContext(TID tid, unsigned primsPerStCompEv, std::string outputPath)
        : tid(tid)
        , primsPerStCompEv(primsPerStCompEv)
        , logger(tid, outputPath)
    {
        /* current shadow memory limit */
        assert(tid <= 128);
        assert(primsPerStCompEv <= 100);
    }

    ~ThreadContext()
    {
        compFlushIfActive();
        commFlushIfActive();
    }

    auto getStats() const -> Stats
    {
        return stats;
    }

    auto onIop() -> void
    {
        commFlushIfActive();
        stComp.incIOP();
        ++std::get<IOP>(stats);
    }

    auto onFlop() -> void
    {
        commFlushIfActive();
        stComp.incFLOP();
        ++std::get<FLOP>(stats);
    }

    auto onRead(const Addr start, const Addr bytes) -> void
    {
        bool isCommEdge = false;

        /* Each byte of the read may have been touched by a different thread */
        for (Addr i = 0; i < bytes; ++i)
        {
            Addr addr = start + i;
            try
            {
                TID writer = shadow.getWriterTID(addr);
                bool isReader= shadow.isReaderTID(addr, tid);

                if (isReader == false)
                    shadow.updateReader(addr, 1, tid);

                if /*comm edge*/((isReader == false) && (writer != tid) && (writer != SO_UNDEF))
                /* XXX treat a read/write to an address with UNDEF thread as a local compute event */
                {
                    isCommEdge = true;
                    compFlushIfActive();
                    stComm.addEdge(writer, shadow.getWriterEID(addr), addr);
                }
                else/*local load, comp event*/
                {
                    commFlushIfActive();
                    stComp.updateReads(addr, 1);
                }
            }
            catch(std::out_of_range &e)
            {
                /* treat as a local event */
                warn(e.what());
                stComp.updateReads(addr, 1);
            }
        }

        /* A situation when a singular memory event is both
         * a communication edge and a local thread read is
         * rare and not robustly accounted for. A single address
         * that is a communication edge counts the whole event as
         * a communication event, and not as part of a
         * computation event */
        if (isCommEdge == false)
            stComp.incReads();

        checkCompFlushLimit();
        ++std::get<READ>(stats);
    }

    auto onWrite(const Addr start, const Addr bytes) -> void
    {
        stComp.incWrites();
        stComp.updateWrites(start, bytes);

        try
        {
            shadow.updateWriter(start, bytes, tid, events);
        }
        catch(std::out_of_range &e)
        {
            warn(e.what());
        }

        checkCompFlushLimit();
        ++std::get<WRITE>(stats);
    }

    auto onSync(unsigned char syncType, Addr syncAddr) -> void
    {
        compFlushIfActive();
        commFlushIfActive();
        logger.flush(syncType, syncAddr, events, tid);
    }

    auto onInstr() -> void
    {
        ++std::get<INSTR>(stats);
    }

    auto checkCompFlushLimit() -> void
    {
        if ((stComp.writes >= primsPerStCompEv) || (stComp.reads >= primsPerStCompEv))
            compFlushIfActive();

        assert(stComp.isActive == false ||
               ((stComp.writes < primsPerStCompEv) && (stComp.reads < primsPerStCompEv)));
    }

    auto compFlushIfActive() -> void
    {
        if (stComp.isActive == true)
        {
            logger.flush(stComp, events, tid);
            stComp.reset();
            if (INCR_EID_OVERFLOW(events))
                fatal("Event ID overflow detected in thread: " + std::to_string(tid));
        }
        assert(stComp.isActive == false);
    }

    auto commFlushIfActive() -> void
    {
        if (stComm.isActive == true)
        {
            logger.flush(stComm, events, tid);
            stComm.reset();
            if (INCR_EID_OVERFLOW(events))
                fatal("Event ID overflow detected in thread: " + std::to_string(tid));
        }
        assert(stComm.isActive == false);
    }

  private:
    /* SynchroTraceGen makes use of 3 SynchroTrace events,
     * i.e. Computation, Communication, and Synchronization.
     *
     * An event aggregates metadata and is eventually flushed to a log.
     * Because there might be trillions or more of SynchroTrace
     * events, each event state is cached here, and reset upon flushing
     *
     * Synchronization events are immediately flushed,
     * so no event state needs to be tracked */
    STCompEvent stComp;
    STCommEvent stComm;

    TID tid;
    unsigned primsPerStCompEv; // compression level of events
    LoggerStrategy logger;
    static STShadowMemory shadow; // Shadow memory is shared amongst all threads

    /* statistics */
    Stats stats{0,0,0,0,0};
    StatCounter events{0};
}; //end ThreadContext

template <class LoggerStrategy>
STShadowMemory ThreadContext<LoggerStrategy>::shadow;

}; //end namespace STGen

#endif