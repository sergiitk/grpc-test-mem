#include <malloc.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdlib>
#include <unistd.h> // For getpid() - standard on POSIX systems
#include <iomanip> // **FIX: Required for std::setprecision and std::fixed**

#include <grpcpp/grpcpp.h>
#include <grpcpp/security/credentials.h>

// --- Configuration Constants ---

// Total size of the temporary file (50 MB)
constexpr size_t ARBITRARY_FILE_SIZE = 50 * 1024 * 1024;
// The amount of data read by read_file (31 MB)
constexpr size_t READ_SIZE =  30 * 1024 * 1024;
// Number of iterations in the main loop
constexpr int NUM_ITERATIONS = 50;

/**
 * @brief Helper function to return the current process Resident Set Size (RSS) in MB.
 *
 * This implementation is non-portable and targets Linux/POSIX environments
 * by reading the /proc/self/status file.
 * @return Current RSS in MB, or 0.0 if unable to read.
 */

 long get_current_rss_mb() {
  // Default is getting memory usage for self (calling process)
  std::string path = "/proc/self/stat";
  std::ifstream stat_stream(path, std::ios_base::in);

  double resident_set = 0.0;
  // Temporary variables for irrelevant leading entries in stats
  std::string temp_pid, comm, state, ppid, pgrp, session, tty_nr;
  std::string tpgid, flags, minflt, cminflt, majflt, cmajflt;
  std::string utime, stime, cutime, cstime, priority, nice;
  std::string O, itrealvalue, starttime, vsize;

  // Get rss to find memory usage
  long rss;
  stat_stream >> temp_pid >> comm >> state >> ppid >> pgrp >> session >>
      tty_nr >> tpgid >> flags >> minflt >> cminflt >> majflt >> cmajflt >>
      utime >> stime >> cutime >> cstime >> priority >> nice >> O >>
      itrealvalue >> starttime >> vsize >> rss;
  stat_stream.close();

  // pid does not connect to an existing process
  assert(!state.empty());

  // Calculations in case x86-64 is configured to use 2MB pages
  long page_size_kb = sysconf(_SC_PAGE_SIZE) / 1024;
  resident_set = rss * page_size_kb;
  // Memory in KB
  return resident_set / 1024;
}


/**
 * @brief Reads a large chunk of data from the file into a temporary buffer.
 *
 * The buffer (data_buffer) is allocated on the stack/heap inside this function
 * and should be automatically released upon function exit.
 * @param file_path The path to the file to read.
 */
void read_file(const std::string& file_path) {
    // Open the file for reading in binary mode
    std::ifstream file(file_path, std::ios::binary);

    if (!file.is_open()) {
        std::cerr << "Error: File '" << file_path << "' not found." << std::endl;
        return;
    }

    // Allocate a buffer on the heap using std::vector. This simulates the
    // temporary memory consumption when reading the large file chunk.
    // In Python, this is what f.read(READ_SIZE) does internally.
    std::vector<char> data_buffer(READ_SIZE);

    // Read the data into the buffer.
    file.read(data_buffer.data(), READ_SIZE);

    file.close();

    // std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    // The data_buffer (and its underlying memory) is automatically released
    // when the function exits (goes out of scope).
}

void print_mallinfo2() {
    struct mallinfo2 mi = mallinfo2();
    std::cout << "  Total non-mmapped bytes (arena):       " << mi.arena / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Number of free chunks (ordblks):       " << mi.ordblks << std::endl;
    std::cout << "  Number of fastbin blocks (smblks):     " << mi.smblks << std::endl;
    std::cout << "  Number of mmapped regions (hblks):     " << mi.hblks << std::endl;

    std::cout << "  Bytes in mmapped regions (hblkhd):     " << mi.hblkhd / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Total allocated space (uordblks):      " << mi.uordblks / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Total free space (fordblks):           " << mi.fordblks / 1024 / 1024 << " MB" << std::endl;
    std::cout << "  Top-most releasable space (keepcost):  " << mi.keepcost  / 1024 / 1024 << " MB" << std::endl;
}


/**
 * @brief Simulates a task that involves repeated memory allocation and resource creation.
 * @param file_path The path to the file used for reading.
 */
void trigger_mem(const std::string& file_path) {
    double initial_rss = get_current_rss_mb();
    std::cout << "PID: " << getpid() << std::endl;
    std::cout << "Initial RSS: " << initial_rss << " MB" << std::endl;
    std::cout << "---------------------------------------------------------" << std::endl;
    // allow to watch the proccess
    // std::this_thread::sleep_for(std::chrono::milliseconds(3000));


    double prev_rss = 0;

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // std::cout << "Iteration " << i + 1 << "/" << NUM_ITERATIONS << " BEGIN" << std::endl;

        // std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // --- 1. File Read Simulation (Memory Spike) ---
        // Create a thread to run the file read function.
        std::thread t(read_file, file_path);
        // read_file(file_path);

        pthread_t native_handle = t.native_handle();

        std::string threadname_str = "MyThread-" + std::to_string(i + 1);
        // pthread_setname_np expects a const char*
        pthread_setname_np(native_handle, threadname_str.c_str());

        // Wait for the thread to finish. This ensures the memory allocated
        // inside 'read_file' is released before the next iteration (unless a leak occurs).
        t.join();

        // --- 2. Resource Creation/Closing Simulation ---
        // Create and immediately close a real gRPC channel.
        std::string address = "localhost:" + std::to_string(4000 + i);
        auto creds = grpc::InsecureChannelCredentials();
        auto channel = grpc::CreateChannel(address, creds);
        // channel goes out of scope here, and its destructor will handle cleanup.

        // --- 3. Monitoring and Output ---
        double current_rss = get_current_rss_mb();
        double diff_from_start = current_rss - initial_rss;
        double diff_from_last = current_rss - prev_rss;
        prev_rss = current_rss;

        // malloc_stats();
        // mall info
        // print_mallinfo2();

        std::cout << "Iteration " << i + 1 << "/" << NUM_ITERATIONS << ": "
                  << "Current RSS: " << std::fixed << std::setprecision(2) << current_rss << " MB | "
                  << "Total increase: " << std::showpos << std::fixed << std::setprecision(2) << diff_from_start << " MB";

        if (diff_from_last != 0) {
            std::cout << " | Delta: " << std::showpos << std::fixed << std::setprecision(2) << diff_from_last << " MB";
        }
        std::cout << std::noshowpos << std::endl;


        // Sleep to mimic real-world processing pause
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        // std::cout << "Iteration " << i + 1 << "/" << NUM_ITERATIONS << " END" << std::endl;
    }
}

/**
 * @brief Mock function to create a large file for the test.
 * Note: This function is commented out in the Python code and replaced by a hardcoded path.
 * We implement it here for completeness, but the main logic still uses the hardcoded path.
 */
void create_mock_file(const std::string& file_path, size_t size) {
    std::cout << "Generating temporary file of size " << size / (1024.0 * 1024.0) << " MB..." << std::endl;
    std::ofstream outfile(file_path, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Error: Could not create mock file at " << file_path << std::endl;
        return;
    }

    // Write null bytes (or random data) to simulate a large file.
    std::vector<char> zero_chunk(1024 * 1024, '\0');
    for (size_t written = 0; written < size; written += zero_chunk.size()) {
        size_t write_size = std::min(zero_chunk.size(), size - written);
        outfile.write(zero_chunk.data(), write_size);
    }
    outfile.close();
    std::cout << "Mock file created at: " << file_path << std::endl;
}

// $ pidstat -t --human -r 1 -e ./bazel-bin/test-mem-leak-read
int main(int argc, char* argv[]) {
    mallopt(M_CHECK_ACTION, 3);
    // 4*1024*1024*sizeof(long)
    mallopt(M_MMAP_THRESHOLD, 0);
    // mallopt(M_MMAP_THRESHOLD, 64 * 1024 * 1024);

    // C++ doesn't use the Python logging module, but we can set up formatting
    // for standard output (std::fixed and std::setprecision are for memory printing).
    std::cout << std::fixed << std::setprecision(2);

    // The Python script used a hardcoded path, so we replicate that here.
    // If you want to use the file generation, uncomment the block below.
    const std::string mock_file_path = "/tmp/tmp_mem_test_file";

    // --- Mock File Creation (Only run this once if the file doesn't exist) ---
    // If you comment this out, ensure the file '/tmp/tmp_mem_test_file' exists
    // and is at least 31 MB large, or replace the path with an existing file.
    create_mock_file(mock_file_path, ARBITRARY_FILE_SIZE);

    // --- Run the Memory Trigger Simulation ---
    trigger_mem(mock_file_path);

    // std::cout << std::endl << "..." << std::endl;
    // for (int i = 0; i < 60; ++i) {
    //     double current_rss = get_current_rss_mb();

    //     std::cout << "Current RSS: " << std::fixed << std::setprecision(2) << current_rss << " MB"
    //               << std::endl;

    //     malloc_trim(0);

    //     // Sleep to mimic real-world processing pause
    //     std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    // }

    // Clean up the mock file after the test
    if (std::remove(mock_file_path.c_str()) != 0) {
        std::cerr << "Warning: Could not delete mock file." << std::endl;
    }

    return 0;
}
