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
    // Метод добавления элемента в очередь
    void push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex); 
// Блокируем мьютекс на время выполнения этого блока
            queue.push(value); // Добавляем элемент в очередь
        }
        cond_var.notify_one();
 // Уведомляем один из ожидающих потоков о наличии нового элемента
    }

    // Извлечения элемента из очереди
    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(mutex); 
// Блокируем мьютекс
        cond_var.wait(lock, [this]() { return !queue.empty(); }); // Ожидаем, пока очередь не станет непустой

        if (queue.empty()) return false; // Если очередь пуста, 
//возвращаем false

        value = queue.front(); // Получаем элемент из начала очереди
        queue.pop(); // Удаляем элемент из очереди
        return true; 
    }

    void stop() {
        cond_var.notify_all(); // Уведомляем все потоки, ожидающие в условной переменной
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
// Добавляет задачи в очередь
void producer(const std::string& input_dir) {
    for (const auto& entry : fs::directory_iterator(input_dir)) { 
// Проходим по всем элементам в директории входных изображений
        if (!entry.is_regular_file()) { 
            continue; // Пропускаем всё, что не является файлом 
        }

        std::string file_name = entry.path().filename().string(); // Получаем имя файла

        // Пропускаем скрытые файлы, начинающиеся с точки (.)
        if (file_name[0] == '.') {
            std::cout << "[Producer] Skipping hidden or system file: " << file_name << std::endl;
            continue;
        }

        // Проверка расширения файла на соответствие изображениям
        std::string ext = entry.path().extension().string();
        if (ext == ".jpeg" || ext == ".jpg" || ext == ".png") {
            std::string file_path = entry.path().string(); 
// Получаем полный путь к файлу
            std::cout << "[Producer] Adding " << file_name << " to queue" << std::endl;
            task_queue.push({file_name, file_path}); 
// Добавляем задачу в очередь 
        } else {
            std::cout << "[Producer] Skipping non-image file: " << file_name << std::endl; 
            // Пропускаем файлы, которые не являются изображениями
        }
    }


    // Сигнал завершения для Consumers: добавляем пустые задачи для завершения потоков-потребителей
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        task_queue.push({"", ""}); 
    }
}

// Consumer: Обрабатывает задачи из очереди
void consumer() {
    while (true) { 
        std::pair<std::string, std::string> task; 
        task_queue.pop(task);  // Извлекаем задачу из очереди

        // Сигнал завершения: если задача пустая, выходим из цикла
        if (task.first.empty() && task.second.empty()) {
            break;
        }

        // Проверка на скрытые файлы внутри consumer
        if (isHiddenFile(task.first)) {
            std::cout << "[Consumer-" << std::this_thread::get_id() << "] Skipping hidden file: " << task.first << std::endl;
            continue; 
        }

        std::cout << "[Consumer-" << std::this_thread::get_id() << "] Processing " << task.first << std::endl;

        // Обработка изображения с использованием OpenCV
        cv::Mat image = cv::imread(task.second); 
        if (image.empty()) { 
            std::cerr << "[Consumer-" << std::this_thread::get_id() << "] Error reading image: " << task.second << std::endl;
            continue; 
        }

        cv::Mat inverted_image; 
        cv::bitwise_not(image, inverted_image);  // Инвертируем цвета изображения

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

    // Запускаем потоки: производитель и потребители
    std::thread producer_thread(producer, INPUT_DIR); 
    std::vector<std::thread> consumer_threads; 
    for (int i = 0; i < NUM_CONSUMERS; ++i) { 
        consumer_threads.emplace_back(consumer); 
 // Создаем потоки-потребители и добавляем их в вектор потоков
    }

    // Ожидание завершения всех потоков: производителя и потребителей
    producer_thread.join();  
    for (auto& thread : consumer_threads) { 
        thread.join();  
    }

    std::cout << "[Main] All tasks completed" << std::endl; 
    return 0;  
}

