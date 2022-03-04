// A broker handles the connection setup using the RDMA CM library. It is single
// threaded but communicates with all other brokers in the system to exchange
// information regarding the underlying RDMA memory configurations.
//
// In the future, this could potentially be replaced with a more robust
// component (e.g., Zookeeper) but for now we stick with a simple approach.

#include <arpa/inet.h>
#include <fcntl.h>
#include <infiniband/verbs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <barrier>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <experimental/coroutine>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>

#include "absl/status/status.h"
#include "rdma_receiver.h"
#include "rome/logging/logging.h"
#include "rome/util/coroutine.h"
#include "rome/util/status_util.h"

namespace rome {

using ::util::Coro;
using ::util::RoundRobinScheduler;
using Scheduler = RoundRobinScheduler;

class RdmaBroker {
 public:
  ~RdmaBroker();
  RdmaBroker(std::string_view id, std::string_view server, uint32_t port,
             RdmaReceiverInterface* receiver);

  RdmaBroker(const RdmaBroker&) = delete;
  RdmaBroker(RdmaBroker&&) = delete;

  absl::Status Connect(std::string_view server, uint32_t port);

  absl::Status Stop();

 private:
  static constexpr int kMaxRetries = 100;

  absl::Status Init();

  Coro HandleConnectionRequests();

  void Run(std::barrier<>* barrier);

  std::string id_;
  std::string server_;
  uint32_t port_;

  // Flag to indicate that the worker thread should terminate.
  std::atomic<bool> terminate_;

  // The working thread that listens and responds to incoming messages.
  struct thread_deleter {
    void operator()(std::thread* thread) { thread->join(); }
  };
  std::unique_ptr<std::thread, thread_deleter> broker_;

  // Status of the broker at any given time.
  absl::Status status_;

  // RDMA CM related members.
  rdma_event_channel* listen_channel_;
  rdma_cm_id* listen_id_;

  // Maintains connections that are forwarded by the broker.
  RdmaReceiverInterface* receiver_;  //! NOT OWNED

  // Runs connection handler coroutine.
  Scheduler scheduler_;
};

}  // namespace rdma