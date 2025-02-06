#include <pqxx/pqxx> // Для работы с PostgreSQL
#include <opencv2/opencv.hpp> // Для работы с изображениями
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <fstream> // Для работы с файлами
#include <curl/curl.h> // Для загрузки изображений по ссылке
#include <thread>
#include <future>

// Структура для хранения данных фильма
struct Movie {
    int id;
    std::string title;
    std::string genre;
    std::string poster_url;
    cv::Mat cover;
};

// Функция для загрузки изображения по URL
size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

cv::Mat loadImageFromURL(const std::string& url) {
    CURL* curl;
    CURLcode res;
    std::string buffer;

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK) {
            std::vector<uchar> data(buffer.begin(), buffer.end());
            return cv::imdecode(data, cv::IMREAD_COLOR);
        }
    }
    return cv::Mat();
}

// Функция для приведения изображения к единому формату
cv::Mat preprocessImage(const cv::Mat &image) {
    if (image.empty()) {
        return cv::Mat();
    }
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(67, 98));
    return resized;
}

// Функция для вычисления среднего значения RGB
cv::Vec3f calculateMeanRGB(const cv::Mat &image) {
    cv::Scalar meanScalar = cv::mean(image);
    return cv::Vec3f(meanScalar[2], meanScalar[1], meanScalar[0]);
}

// Функция для расчета косинусного расстояния
float cosineDistance(const cv::Vec3f &a, const cv::Vec3f &b) {
    return 1.0f - (a.dot(b) / (cv::norm(a) * cv::norm(b)));
}

// Функция для загрузки фильмов с многопоточной загрузкой изображений
std::vector<Movie> loadMoviesFromDatabase(const std::string &connectionString) {
    std::vector<Movie> movies;
    try {
        pqxx::connection conn(connectionString);
        pqxx::work txn(conn);
        pqxx::result result = txn.exec("SELECT id, title, genre, poster_link FROM movies;");

        std::vector<std::future<cv::Mat>> futures;
        for (const auto &row : result) {
            Movie movie;
            movie.id = row["id"].as<int>();
            movie.title = row["title"].as<std::string>();
            movie.genre = row["genre"].as<std::string>();
            movie.poster_url = row["poster_link"].as<std::string>();

            futures.push_back(std::async(std::launch::async, loadImageFromURL, movie.poster_url));
            movies.push_back(movie);
        }

        for (size_t i = 0; i < movies.size(); ++i) {
            movies[i].cover = preprocessImage(futures[i].get());
        }
    } catch (const std::exception &e) {
        std::cerr << "Исключение: " << e.what() << std::endl;
    }
    return movies;
}

// Функция для рекомендаций
std::vector<Movie> recommendMovies(const Movie &inputMovie, const std::vector<Movie> &movies, int topN = 5) {
    std::vector<std::pair<float, Movie>> distances;
    cv::Vec3f inputMeanRGB = calculateMeanRGB(inputMovie.cover);
    
    for (const auto &movie : movies) {
        cv::Vec3f meanRGB = calculateMeanRGB(movie.cover);
        float distance = cosineDistance(inputMeanRGB, meanRGB);
        distances.emplace_back(distance, movie);
    }
    
    std::sort(distances.begin(), distances.end(), [](const auto &a, const auto &b) {
        return a.first < b.first;
    });
    
    std::vector<Movie> recommendations;
    for (int i = 0; i < std::min(topN, static_cast<int>(distances.size())); ++i) {
        recommendations.push_back(distances[i].second);
    }
    return recommendations;
}

int main() {
    std::string connectionString = "dbname=image_rec user=postgres password=123123";
    std::vector<Movie> movies = loadMoviesFromDatabase(connectionString);

    std::string userTitle, userPosterURL;
    std::cout << "Введите название фильма: ";
    std::getline(std::cin, userTitle);
    std::cout << "Введите URL обложки фильма: ";
    std::getline(std::cin, userPosterURL);

    cv::Mat userCover = preprocessImage(loadImageFromURL(userPosterURL));
    if (userCover.empty()) {
        std::cerr << "Ошибка: не удалось загрузить изображение пользователя." << std::endl;
        return 1;
    }

    Movie userMovie = {0, userTitle, "N/A", userPosterURL, userCover};
    std::vector<Movie> recommendations = recommendMovies(userMovie, movies);

    std::cout << "Рекомендованные фильмы:\n";
    for (const auto &movie : recommendations) {
        std::cout << "Название: " << movie.title << ", Жанр: " << movie.genre << ", Постер: " << movie.poster_url << std::endl;
    }

    return 0;
}
