#include <iostream>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <filesystem>
#include <opencv2/opencv.hpp> // Используем OpenCV для обработки изображений

namespace fs = std::filesystem;

// Блокирующая очередь
template <typename T>
class BlockingQueue {
private:
    std::queue<T> queue;
    std::mutex mutex;
    std::condition_variable cond_var;

public:
    void push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            queue.push(value);
        }
        cond_var.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex);
        cond_var.wait(lock, [this]() { return !queue.empty(); });

        if (queue.empty()) return false;

        value = queue.front();
        queue.pop();
        return true;
    }

    void stop() {
        cond_var.notify_all();
    }
};

// Константы
const std::string INPUT_DIR = "input_images";
const std::string OUTPUT_DIR = "output_images";
const int NUM_CONSUMERS = 4;

// Очереди
BlockingQueue<std::pair<std::string, std::string>> task_queue;

// Проверка, является ли файл скрытым
bool isHiddenFile(const std::string& file_name) {
    return file_name[0] == '.';
}

void producer(const std::string& input_dir) {
    for (const auto& entry : fs::directory_iterator(input_dir)) {
        if (!entry.is_regular_file()) {
            continue; // Пропускаем всё, что не является файлом
        }

        std::string file_name = entry.path().filename().string();

        // Пропускаем скрытые файлы, начинающиеся с точки (.)
        if (file_name[0] == '.') {
            std::cout << "[Producer] Skipping hidden or system file: " << file_name << std::endl;
            continue;
        }

        // Проверка расширения файла
        std::string ext = entry.path().extension().string();
        if (ext == ".jpeg" || ext == ".jpg" || ext == ".png") {
            std::string file_path = entry.path().string();
            std::cout << "[Producer] Adding " << file_name << " to queue" << std::endl;
            task_queue.push({file_name, file_path});
        } else {
            std::cout << "[Producer] Skipping non-image file: " << file_name << std::endl;
        }
    }

    // Сигнал завершения для Consumers
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        task_queue.push({"", ""});
    }
}


// Consumer: Обрабатывает задачи из очереди
void consumer() {
    while (true) {
        std::pair<std::string, std::string> task;
        task_queue.pop(task);

        // Сигнал завершения
        if (task.first.empty() && task.second.empty()) {
            break;
        }

        // Проверка на скрытые файлы внутри consumer
        if (isHiddenFile(task.first)) {
            std::cout << "[Consumer-" << std::this_thread::get_id() << "] Skipping hidden file: " << task.first << std::endl;
            continue;
        }

        std::cout << "[Consumer-" << std::this_thread::get_id() << "] Processing " << task.first << std::endl;

        // Обработка изображения
        cv::Mat image = cv::imread(task.second);
        if (image.empty()) {
            std::cerr << "[Consumer-" << std::this_thread::get_id() << "] Error reading image: " << task.second << std::endl;
            continue;
        }

        cv::Mat inverted_image;
        cv::bitwise_not(image, inverted_image);

        std::string output_path = OUTPUT_DIR + "/inverted_" + task.first;
        if (cv::imwrite(output_path, inverted_image)) {
            std::cout << "[Consumer-" << std::this_thread::get_id() << "] Saved inverted image to: " << output_path << std::endl;
        } else {
            std::cerr << "[Consumer-" << std::this_thread::get_id() << "] Error saving image: " << output_path << std::endl;
        }
    }

    std::cout << "[Consumer-" << std::this_thread::get_id() << "] Exiting" << std::endl;
}

int main() {
    // Создаем выходную директорию, если её нет
    if (!fs::exists(OUTPUT_DIR)) {
        fs::create_directory(OUTPUT_DIR);
    }

    // Запускаем потоки Producer-Customer
    std::thread producer_thread(producer, INPUT_DIR);
    std::vector<std::thread> consumer_threads;
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumer_threads.emplace_back(consumer);
    }

    // Ожидание завершения всех потоков
    producer_thread.join();
    for (auto& thread : consumer_threads) {
        thread.join();
    }

    std::cout << "[Main] All tasks completed" << std::endl;
    return 0;
}
