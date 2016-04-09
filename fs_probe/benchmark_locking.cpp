/* benchmark_locking.cpp
Test the performance of various file locking mechanisms
(C) 2016 Niall Douglas http://www.nedprod.com/
File Created: Mar 2016


Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#define _CRT_SECURE_NO_WARNINGS 1

#include "boost/afio/v2/algorithm/shared_fs_mutex/atomic_append.hpp"
#include "boost/afio/v2/algorithm/shared_fs_mutex/byte_ranges.hpp"
#include "boost/afio/v2/algorithm/shared_fs_mutex/lock_files.hpp"
#include "boost/afio/v2/detail/child_process.hpp"

#include <iostream>
#include <vector>

#ifdef _WIN32
#undef _CRT_NONSTDC_DEPRECATE
#define _CRT_NONSTDC_DEPRECATE(a)
#include <conio.h>  // for kbhit()
#endif

namespace afio = BOOST_AFIO_V2_NAMESPACE;

int main(int argc, char *argv[])
{
  if(argc < 4)
  {
    std::cerr << "Usage: " << argv[0] << " [!]<atomic_append|byte_ranges|lock_files> <entities> <no of waiters>" << std::endl;
    return 1;
  }
  // Am I the master process?
  if(strcmp(argv[1], "spawned") && strcmp(argv[1], "!spawned"))
  {
    size_t waiters = atoi(argv[3]);
    if(!waiters || !atoi(argv[2]))
    {
      std::cerr << "Usage: " << argv[0] << " [!]<atomic_append|byte_ranges|lock_files> <entities> <no of waiters>" << std::endl;
      return 1;
    }
    std::vector<afio::detail::child_process> children;
    auto mypath = afio::detail::current_process_path();
#ifdef UNICODE
    std::vector<afio::stl1z::filesystem::path::string_type> args = {L"spawned", L"", L"", L"", L"00"};
    args[1].resize(strlen(argv[1]));
    for(size_t n = 0; n < args[1].size(); n++)
      args[1][n] = argv[1][n];
    args[2].resize(strlen(argv[2]));
    for(size_t n = 0; n < args[2].size(); n++)
      args[2][n] = argv[2][n];
    args[3].resize(strlen(argv[3]));
    for(size_t n = 0; n < args[3].size(); n++)
      args[3][n] = argv[3][n];
#else
    std::vector<afio::stl1z::filesystem::path::string_type> args = {"spawned", argv[1], argv[2], argv[3], "00"};
#endif
    auto env = afio::detail::current_process_env();
    std::cout << "Launching " << waiters << " copies of myself as a child process ..." << std::endl;
    for(size_t n = 0; n < waiters; n++)
    {
      if(n >= 10)
      {
        args[3][0] = (char) ('0' + (n / 10));
        args[3][1] = (char) ('0' + (n % 10));
      }
      else
      {
        args[3][0] = (char) ('0' + n);
        args[3][1] = 0;
      }
      auto child = afio::detail::child_process::launch(mypath, args, env);
      if(child.has_error())
      {
        std::cerr << "FATAL: Child " << n << " could not be launched due to " << child.get_error().message() << std::endl;
        return 1;
      }
      children.push_back(std::move(child.get()));
    }
    // Wait for all children to tell me they are ready
    char buffer[1024];
    std::cout << "Waiting for all children to become ready ..." << std::endl;
    for(auto &child : children)
    {
      auto &i = child.cout();
      if(!i.getline(buffer, sizeof(buffer)))
      {
        std::cerr << "ERROR: Child seems to have vanished!" << std::endl;
        return 1;
      }
      if(0 != strncmp(buffer, "READY", 5))
      {
        std::cerr << "ERROR: Child wrote unexpected output '" << buffer << "'" << std::endl;
        return 1;
      }
    }
    std::cout << "Starting benchmark ..." << std::endl;
    // Issue go command to all children
    for(auto &child : children)
      child.cin() << "GO" << std::endl;
    // Wait for benchmark to complete
    std::this_thread::sleep_for(std::chrono::seconds(5));
    std::cout << "Stopping benchmark and telling children to report results ..." << std::endl;
    // Tell children to quit
    for(auto &child : children)
      child.cin() << "STOP" << std::endl;
    unsigned long long results = 0, result;
    std::cout << std::endl;
    for(size_t n = 0; n < children.size(); n++)
    {
      auto &child = children[n];
      if(!child.cout().getline(buffer, sizeof(buffer)) || 0 != strncmp(buffer, "RESULTS(", 8))
      {
        std::cerr << "ERROR: Child wrote unexpected output '" << buffer << "'" << std::endl;
        return 1;
      }
      result = atol(&buffer[8]);
      std::cout << "Child " << n << " reports result " << result << std::endl;
      results += result;
    }
    std::cout << "Total result: " << results << std::endl;
    return 0;
  }

  if(argc < 6)
  {
    std::cerr << "ERROR: args too short" << std::endl;
    return 1;
  }
  enum class lock_algorithm
  {
    unknown,
    atomic_append,
    byte_ranges,
    lock_files
  } test = lock_algorithm::unknown;
  bool contended = true;
  if(!strcmp(argv[2], "atomic_append"))
    test = lock_algorithm::atomic_append;
  else if(!strcmp(argv[2], "byte_ranges"))
    test = lock_algorithm::byte_ranges;
  else if(!strcmp(argv[2], "lock_files"))
    test = lock_algorithm::lock_files;
  else if(!strcmp(argv[2], "!atomic_append"))
  {
    test = lock_algorithm::atomic_append;
    contended = false;
  }
  else if(!strcmp(argv[2], "!byte_ranges"))
  {
    test = lock_algorithm::byte_ranges;
    contended = false;
  }
  else if(!strcmp(argv[2], "!lock_files"))
  {
    test = lock_algorithm::lock_files;
    contended = false;
  }
  if(test == lock_algorithm::unknown)
  {
    std::cerr << "ERROR: unknown test requested" << std::endl;
    return 1;
  }
  size_t total_locks = atoi(argv[3]), waiters = atoi(argv[4]), this_child = atoi(argv[5]), count = 0;
  (void) waiters;
  if(!total_locks)
  {
    std::cerr << "ERROR: unknown total locks requested" << std::endl;
    return 1;
  }
  // I am a spawned child. Tell parent I am ready.
  std::cout << "READY(" << this_child << ")" << std::endl;
  // Wait for parent to let me proceed
  std::atomic<int> done(-1);
  std::thread worker([test, contended, total_locks, this_child, &done, &count] {
    std::unique_ptr<afio::algorithm::shared_fs_mutex::shared_fs_mutex> algorithm;
    switch(test)
    {
    case lock_algorithm::atomic_append:
    {
      auto v = afio::algorithm::shared_fs_mutex::atomic_append::fs_mutex_append("lockfile");
      if(v.has_error())
      {
        std::cerr << "ERROR: Creation of lock algorithm returns " << v.get_error().message() << std::endl;
        std::terminate();
      }
      algorithm = std::make_unique<afio::algorithm::shared_fs_mutex::atomic_append>(std::move(v.get()));
      break;
    }
    case lock_algorithm::byte_ranges:
    {
      auto v = afio::algorithm::shared_fs_mutex::byte_ranges::fs_mutex_byte_ranges("lockfile");
      if(v.has_error())
      {
        std::cerr << "ERROR: Creation of lock algorithm returns " << v.get_error().message() << std::endl;
        std::terminate();
      }
      algorithm = std::make_unique<afio::algorithm::shared_fs_mutex::byte_ranges>(std::move(v.get()));
      break;
    }
    case lock_algorithm::lock_files:
    {
      auto v = afio::algorithm::shared_fs_mutex::lock_files::fs_mutex_lock_files(".");
      if(v.has_error())
      {
        std::cerr << "ERROR: Creation of lock algorithm returns " << v.get_error().message() << std::endl;
        std::terminate();
      }
      algorithm = std::make_unique<afio::algorithm::shared_fs_mutex::lock_files>(std::move(v.get()));
      break;
    }
    case lock_algorithm::unknown:
      break;
    }
    // Create entities named 0 to total_locks
    std::vector<afio::algorithm::shared_fs_mutex::shared_fs_mutex::entity_type> entities(total_locks);
    for(size_t n = 0; n < total_locks; n++)
    {
      if(contended)
      {
        entities[n].value = n;
        entities[n].exclusive = true;
      }
      else
      {
        entities[n].value = (this_child << 16) + n;  // guaranteed unique
        entities[n].exclusive = true;
      }
    }
    while(done == -1)
      std::this_thread::yield();
    while(!done)
    {
      auto result = algorithm->lock(afio::as_span(entities));
      if(result.has_error())
      {
        std::cerr << "ERROR: Lock algorithms returns " << result.get_error().message() << std::endl;
        std::terminate();
      }
      ++count;
      // On destruction, will unlock the lock.
      auto guard = std::move(result.get());
    }
  });
  if(!strcmp(argv[1], "!spawned"))
  {
    auto lastcount = count;
    size_t secs = 0;
    done = 0;
    while(!kbhit())
    {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      ++secs;
      std::cout << "\ncount=" << count << " (+" << (count - lastcount) << "), average=" << (count / secs) << std::endl;
      lastcount = count;
#if 1
      auto it = afio::log().cbegin();
      for(size_t n = 0; n < 10; n++)
      {
        if(it == afio::log().cend())
          break;
        std::cout << "   " << *it;
        ++it;
      }
#endif
    }
    done = 1;
    worker.join();
  }
  else
    for(;;)
    {
      char buffer[1024];
      // This blocks
      if(!std::cin.getline(buffer, sizeof(buffer)))
      {
        return 1;
      }
      if(0 == strcmp(buffer, "GO"))
      {
        // Launch worker thread
        done = 0;
      }
      else if(0 == strcmp(buffer, "STOP"))
      {
        done = 1;
        worker.join();
        std::cout << "RESULTS(" << count << ")" << std::endl;
        return 0;
      }
    }
}
