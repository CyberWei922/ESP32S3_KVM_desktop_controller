import Foundation
import Kit

enum DashboardWeatherProvider: String, CaseIterable {
    case openMeteo = "open_meteo"
    case qweather = "qweather"
    case amap = "amap"

    var title: String {
        switch self {
        case .openMeteo: return "Open-Meteo"
        case .qweather: return "和风天气"
        case .amap: return "高德天气"
        }
    }
}

struct DashboardWeatherCurrent: Encodable {
    let temperatureC, highC, lowC, weatherCode: Int
    let isDay: Bool
}

struct DashboardWeatherDaily: Encodable {
    let day: String
    let highC, lowC, weatherCode: Int
}

struct DashboardWeatherHourly: Encodable {
    let hour, temperatureC, weatherCode: Int
    let isDay: Bool
}

struct DashboardWeather: Encodable {
    let provider, cityCode, cityName: String
    let updatedAtMilliseconds: Int64
    let current: DashboardWeatherCurrent
    let daily: [DashboardWeatherDaily]
    let hourly: [DashboardWeatherHourly]
}

struct DashboardQuote: Encodable {
    let price: Double
    let changePercent: Double
}

struct DashboardMarkets: Encodable {
    let updatedAtMilliseconds: Int64
    let btcUsdt, dogeUsdt, usdCny: DashboardQuote
}

struct MonitorDashboardMessage: Encodable {
    let type = "dashboard"
    let protocolVersion = 1
    let deviceID: String
    let platform = "macos"
    let sequence: UInt64
    let timestampMilliseconds: Int64
    let weather: DashboardWeather?
    let markets: DashboardMarkets?
    let error: String?
}

final class DashboardDataService {
    static let shared = DashboardDataService()

    private enum Key {
        static let provider = "statsforkvm_dashboard_weather_provider"
        static let cityCode = "statsforkvm_dashboard_city_code"
        static let cityName = "statsforkvm_dashboard_city_name"
        static let qweatherHost = "statsforkvm_dashboard_qweather_host"
        static let qweatherKey = "statsforkvm_dashboard_qweather_key"
        static let amapKey = "statsforkvm_dashboard_amap_key"
    }

    private struct Location {
        let code, name: String
        let latitude, longitude: Double
    }

    private let lock = NSLock()
    private let session: URLSession = {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.timeoutIntervalForRequest = 12
        configuration.timeoutIntervalForResource = 20
        return URLSession(configuration: configuration)
    }()
    private var weatherValue: DashboardWeather?
    private var marketValue: DashboardMarkets?
    private var errorValue: String?
    private var weatherFetchInFlight = false
    private var marketFetchInFlight = false
    private var lastWeatherFetch = Date.distantPast
    private var lastMarketFetch = Date.distantPast

    var provider: DashboardWeatherProvider {
        DashboardWeatherProvider(rawValue: Store.shared.string(key: Key.provider,
                                                                defaultValue: DashboardWeatherProvider.openMeteo.rawValue)) ?? .openMeteo
    }
    var cityCode: String { Store.shared.string(key: Key.cityCode, defaultValue: "Shanghai") }
    var cityName: String { Store.shared.string(key: Key.cityName, defaultValue: "—") }
    var qweatherHost: String { Store.shared.string(key: Key.qweatherHost, defaultValue: "") }
    var qweatherKey: String { Store.shared.string(key: Key.qweatherKey, defaultValue: "") }
    var amapKey: String { Store.shared.string(key: Key.amapKey, defaultValue: "") }

    private init() {}

    func update(provider: DashboardWeatherProvider, cityCode: String,
                qweatherHost: String, qweatherKey: String, amapKey: String) {
        Store.shared.set(key: Key.provider, value: provider.rawValue)
        Store.shared.set(key: Key.cityCode, value: cityCode.trimmingCharacters(in: .whitespacesAndNewlines))
        Store.shared.set(key: Key.qweatherHost,
                         value: qweatherHost.trimmingCharacters(in: .whitespacesAndNewlines))
        Store.shared.set(key: Key.qweatherKey,
                         value: qweatherKey.trimmingCharacters(in: .whitespacesAndNewlines))
        Store.shared.set(key: Key.amapKey,
                         value: amapKey.trimmingCharacters(in: .whitespacesAndNewlines))
        self.lock.lock()
        self.lastWeatherFetch = .distantPast
        self.weatherValue = nil
        self.errorValue = nil
        self.lock.unlock()
        self.refresh(forceWeather: true)
    }

    func refresh(forceWeather: Bool = false) {
        let now = Date()
        self.lock.lock()
        let fetchWeather = !self.weatherFetchInFlight &&
            (forceWeather || now.timeIntervalSince(self.lastWeatherFetch) >= 15 * 60)
        let fetchMarkets = !self.marketFetchInFlight &&
            now.timeIntervalSince(self.lastMarketFetch) >= 10
        if fetchWeather { self.weatherFetchInFlight = true }
        if fetchMarkets { self.marketFetchInFlight = true }
        self.lock.unlock()
        if fetchWeather { self.fetchWeather() }
        if fetchMarkets { self.fetchMarkets() }
    }

    func message(deviceID: String, sequence: UInt64) -> MonitorDashboardMessage {
        self.lock.lock()
        let weather = self.weatherValue
        let markets = self.marketValue
        let error = self.errorValue ?? ((weather == nil && markets == nil) ? "dashboard_loading" : nil)
        self.lock.unlock()
        return MonitorDashboardMessage(
            deviceID: deviceID,
            sequence: sequence,
            timestampMilliseconds: Self.nowMilliseconds,
            weather: weather,
            markets: markets,
            error: error
        )
    }

    private func fetchWeather() {
        let query = self.cityCode
        guard !query.isEmpty else { self.finishWeather(nil, error: "city_code_missing"); return }
        switch self.provider {
        case .openMeteo: self.resolveOpenMeteo(query: query)
        case .qweather: self.resolveQWeather(query: query)
        case .amap: self.resolveAMap(query: query)
        }
    }

    private func resolveOpenMeteo(query: String) {
        let numeric = query.allSatisfy(\.isNumber)
        let endpoint = numeric
            ? "https://geocoding-api.open-meteo.com/v1/get?id=\(query)"
            : "https://geocoding-api.open-meteo.com/v1/search?name=\(Self.escape(query))&count=1&language=zh&countryCode=CN"
        self.json(endpoint) { [weak self] json in
            guard let self else { return }
            let result: [String: Any]?
            if numeric { result = json }
            else { result = (json?["results"] as? [[String: Any]])?.first }
            guard let result,
                  let name = result["name"] as? String,
                  let latitude = Self.double(result["latitude"]),
                  let longitude = Self.double(result["longitude"]) else {
                self.finishWeather(nil, error: "city_not_found"); return
            }
            let code = String(result["id"] as? Int ?? Int(query) ?? 0)
            self.fetchOpenMeteoForecast(location: Location(code: code == "0" ? query : code,
                                                             name: name,
                                                             latitude: latitude,
                                                             longitude: longitude))
        }
    }

    private func fetchOpenMeteoForecast(location: Location) {
        let endpoint = "https://api.open-meteo.com/v1/forecast?latitude=\(location.latitude)&longitude=\(location.longitude)&current=temperature_2m,weather_code,is_day&hourly=temperature_2m,weather_code,is_day&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4"
        self.json(endpoint) { [weak self] json in
            guard let self, let json,
                  let current = json["current"] as? [String: Any],
                  let daily = json["daily"] as? [String: Any],
                  let hourly = json["hourly"] as? [String: Any],
                  let currentTemperature = Self.double(current["temperature_2m"]),
                  let currentCode = Self.int(current["weather_code"]),
                  let high = (daily["temperature_2m_max"] as? [Any])?.compactMap(Self.double),
                  let low = (daily["temperature_2m_min"] as? [Any])?.compactMap(Self.double),
                  let dailyCode = (daily["weather_code"] as? [Any])?.compactMap(Self.int),
                  let dates = daily["time"] as? [String],
                  high.count >= 4, low.count >= 4, dailyCode.count >= 4, dates.count >= 4 else {
                self?.finishWeather(nil, error: "weather_response_invalid"); return
            }
            let hourlyTimes = hourly["time"] as? [String] ?? []
            let hourlyTemps = (hourly["temperature_2m"] as? [Any])?.compactMap(Self.double) ?? []
            let hourlyCodes = (hourly["weather_code"] as? [Any])?.compactMap(Self.int) ?? []
            let hourlyDay = (hourly["is_day"] as? [Any])?.compactMap(Self.int) ?? []
            let start = Self.nextHourlyIndex(times: hourlyTimes)
            guard hourlyTimes.count >= start + 5, hourlyTemps.count >= start + 5,
                  hourlyCodes.count >= start + 5, hourlyDay.count >= start + 5 else {
                self.finishWeather(nil, error: "hourly_forecast_incomplete"); return
            }
            let dayFormatter = DateFormatter()
            dayFormatter.locale = Locale(identifier: "en_US_POSIX")
            dayFormatter.dateFormat = "yyyy-MM-dd"
            let weekday = DateFormatter()
            weekday.locale = Locale(identifier: "en_US_POSIX")
            weekday.dateFormat = "EEE"
            let days = (1...3).map { index in
                DashboardWeatherDaily(day: dayFormatter.date(from: dates[index]).map { weekday.string(from: $0).uppercased() } ?? "---",
                                      highC: Int(high[index].rounded()), lowC: Int(low[index].rounded()),
                                      weatherCode: dailyCode[index])
            }
            let hours = (start..<(start + 5)).map { index in
                DashboardWeatherHourly(hour: Self.hour(from: hourlyTimes[index]),
                                       temperatureC: Int(hourlyTemps[index].rounded()),
                                       weatherCode: hourlyCodes[index], isDay: hourlyDay[index] == 1)
            }
            self.finishWeather(DashboardWeather(
                provider: DashboardWeatherProvider.openMeteo.rawValue,
                cityCode: location.code, cityName: Self.displayCityName(location.name),
                updatedAtMilliseconds: Self.nowMilliseconds,
                current: DashboardWeatherCurrent(temperatureC: Int(currentTemperature.rounded()),
                                                 highC: Int(high[0].rounded()), lowC: Int(low[0].rounded()),
                                                 weatherCode: currentCode,
                                                 isDay: Self.int(current["is_day"]) == 1),
                daily: days, hourly: hours
            ), error: nil)
        }
    }

    private func resolveQWeather(query: String) {
        let host = self.qweatherHost.replacingOccurrences(of: "https://", with: "")
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        let key = self.qweatherKey
        guard !host.isEmpty, !key.isEmpty else {
            self.finishWeather(nil, error: "qweather_credentials_missing"); return
        }
        let endpoint = "https://\(host)/geo/v2/city/lookup?location=\(Self.escape(query))&range=cn&number=1&lang=zh"
        self.json(endpoint, headers: ["X-QW-Api-Key": key]) { [weak self] json in
            guard let self,
                  let result = (json?["location"] as? [[String: Any]])?.first,
                  let code = result["id"] as? String,
                  let name = result["name"] as? String,
                  let latitude = Self.double(result["lat"]),
                  let longitude = Self.double(result["lon"]) else {
                self?.finishWeather(nil, error: "qweather_city_not_found"); return
            }
            self.fetchQWeatherForecast(location: Location(code: code, name: name,
                                                           latitude: latitude, longitude: longitude),
                                       host: host, key: key)
        }
    }

    private func fetchQWeatherForecast(location: Location, host: String, key: String) {
        let base = "https://\(host)"
        let paths = [
            "\(base)/weather/v1/current/\(location.latitude)/\(location.longitude)?lang=zh",
            "\(base)/weather/v1/daily/\(location.latitude)/\(location.longitude)?days=4&localTime=true&lang=zh",
            "\(base)/weather/v1/hourly/\(location.latitude)/\(location.longitude)?hours=6&localTime=true&lang=zh"
        ]
        let group = DispatchGroup()
        let resultLock = NSLock()
        var results = Array<[String: Any]?>(repeating: nil, count: 3)
        for (index, path) in paths.enumerated() {
            group.enter()
            self.json(path, headers: ["X-QW-Api-Key": key]) { json in
                resultLock.lock(); results[index] = json; resultLock.unlock(); group.leave()
            }
        }
        group.notify(queue: .global(qos: .utility)) { [weak self] in
            guard let self,
                  let currentJSON = results[0], let dailyJSON = results[1], let hourlyJSON = results[2],
                  let current = (currentJSON["current"] as? [String: Any]) ?? (currentJSON["now"] as? [String: Any]),
                  let daysJSON = dailyJSON["days"] as? [[String: Any]], daysJSON.count >= 4,
                  let hoursJSON = hourlyJSON["hours"] as? [[String: Any]], hoursJSON.count >= 5,
                  let currentTemp = Self.temperature(current),
                  let currentCode = Self.conditionCode(current) else {
                self?.finishWeather(nil, error: "qweather_response_invalid"); return
            }
            let dayValues = daysJSON.compactMap(Self.qweatherDay)
            let hourValues = hoursJSON.prefix(5).compactMap(Self.qweatherHour)
            guard dayValues.count >= 4, hourValues.count == 5 else {
                self.finishWeather(nil, error: "qweather_forecast_incomplete"); return
            }
            self.finishWeather(DashboardWeather(
                provider: DashboardWeatherProvider.qweather.rawValue,
                cityCode: location.code, cityName: Self.displayCityName(location.name),
                updatedAtMilliseconds: Self.nowMilliseconds,
                current: DashboardWeatherCurrent(temperatureC: currentTemp,
                                                 highC: dayValues[0].highC, lowC: dayValues[0].lowC,
                                                 weatherCode: currentCode,
                                                 isDay: (Self.int(current["isDay"]) ?? 1) == 1),
                daily: Array(dayValues[1...3]), hourly: hourValues
            ), error: nil)
        }
    }

    private func resolveAMap(query: String) {
        let key = self.amapKey
        guard !key.isEmpty else { self.finishWeather(nil, error: "amap_credentials_missing"); return }
        let endpoint = "https://restapi.amap.com/v3/config/district?keywords=\(Self.escape(query))&subdistrict=0&extensions=base&key=\(Self.escape(key))"
        self.json(endpoint) { [weak self] json in
            guard let self,
                  let district = (json?["districts"] as? [[String: Any]])?.first,
                  let code = district["adcode"] as? String,
                  let name = district["name"] as? String,
                  let center = district["center"] as? String else {
                self?.finishWeather(nil, error: "amap_city_not_found"); return
            }
            let coordinate = center.split(separator: ",").compactMap { Double($0) }
            guard coordinate.count == 2 else {
                self.finishWeather(nil, error: "amap_city_not_found"); return
            }
            self.fetchAMapForecast(location: Location(code: code, name: name,
                                                       latitude: coordinate[1], longitude: coordinate[0]),
                                   key: key)
        }
    }

    private func fetchAMapForecast(location: Location, key: String) {
        let hourly = "https://api.open-meteo.com/v1/forecast?latitude=\(location.latitude)&longitude=\(location.longitude)&hourly=temperature_2m,weather_code,is_day&timezone=auto&forecast_days=2"
        let paths = [
            "https://restapi.amap.com/v3/weather/weatherInfo?city=\(location.code)&extensions=base&key=\(Self.escape(key))",
            "https://restapi.amap.com/v3/weather/weatherInfo?city=\(location.code)&extensions=all&key=\(Self.escape(key))",
            hourly
        ]
        let group = DispatchGroup()
        let resultLock = NSLock()
        var results = Array<[String: Any]?>(repeating: nil, count: 3)
        for (index, path) in paths.enumerated() {
            group.enter()
            self.json(path) { json in
                resultLock.lock(); results[index] = json; resultLock.unlock(); group.leave()
            }
        }
        group.notify(queue: .global(qos: .utility)) { [weak self] in
            guard let self,
                  let live = (results[0]?["lives"] as? [[String: Any]])?.first,
                  let forecast = (results[1]?["forecasts"] as? [[String: Any]])?.first,
                  let casts = forecast["casts"] as? [[String: Any]], casts.count >= 4,
                  let currentTemperature = Self.int(live["temperature"]),
                  let currentText = live["weather"] as? String,
                  let hourlyJSON = results[2]?["hourly"] as? [String: Any] else {
                self?.finishWeather(nil, error: "amap_response_invalid"); return
            }
            let times = hourlyJSON["time"] as? [String] ?? []
            let temperatures = (hourlyJSON["temperature_2m"] as? [Any])?.compactMap(Self.double) ?? []
            let codes = (hourlyJSON["weather_code"] as? [Any])?.compactMap(Self.int) ?? []
            let dayFlags = (hourlyJSON["is_day"] as? [Any])?.compactMap(Self.int) ?? []
            let start = Self.nextHourlyIndex(times: times)
            guard times.count >= start + 5, temperatures.count >= start + 5,
                  codes.count >= start + 5, dayFlags.count >= start + 5,
                  let todayHigh = Self.int(casts[0]["daytemp"]),
                  let todayLow = Self.int(casts[0]["nighttemp"]) else {
                self.finishWeather(nil, error: "amap_forecast_incomplete"); return
            }
            let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX"); formatter.dateFormat = "yyyy-MM-dd"
            let weekday = DateFormatter(); weekday.locale = Locale(identifier: "en_US_POSIX"); weekday.dateFormat = "EEE"
            var days: [DashboardWeatherDaily] = []
            for index in 1...3 {
                guard let high = Self.int(casts[index]["daytemp"]),
                      let low = Self.int(casts[index]["nighttemp"]),
                      let text = casts[index]["dayweather"] as? String else {
                    self.finishWeather(nil, error: "amap_forecast_incomplete"); return
                }
                let day = (casts[index]["date"] as? String).flatMap(formatter.date(from:))
                days.append(DashboardWeatherDaily(day: day.map { weekday.string(from: $0).uppercased() } ?? "---",
                                                  highC: high, lowC: low,
                                                  weatherCode: Self.amapWeatherCode(text)))
            }
            let hours = (start..<(start + 5)).map { index in
                DashboardWeatherHourly(hour: Self.hour(from: times[index]),
                                       temperatureC: Int(temperatures[index].rounded()),
                                       weatherCode: codes[index], isDay: dayFlags[index] == 1)
            }
            let currentHour = Calendar.current.component(.hour, from: Date())
            self.finishWeather(DashboardWeather(
                provider: DashboardWeatherProvider.amap.rawValue,
                cityCode: location.code, cityName: Self.displayCityName(location.name),
                updatedAtMilliseconds: Self.nowMilliseconds,
                current: DashboardWeatherCurrent(temperatureC: currentTemperature,
                                                 highC: todayHigh, lowC: todayLow,
                                                 weatherCode: Self.amapWeatherCode(currentText),
                                                 isDay: currentHour >= 6 && currentHour < 19),
                daily: days, hourly: hours
            ), error: nil)
        }
    }

    private func fetchMarkets() {
        let group = DispatchGroup()
        let resultLock = NSLock()
        var crypto: [String: DashboardQuote] = [:]
        var exchange: DashboardQuote?
        group.enter()
        let symbols = Self.escape("[\"BTCUSDT\",\"DOGEUSDT\"]")
        self.json("https://api.binance.com/api/v3/ticker/24hr?symbols=\(symbols)") { json in
            if let rows = json?["_array"] as? [[String: Any]] {
                resultLock.lock()
                for row in rows {
                    if let symbol = row["symbol"] as? String,
                       let price = Self.double(row["lastPrice"]),
                       let change = Self.double(row["priceChangePercent"]) {
                        crypto[symbol] = DashboardQuote(price: price, changePercent: change)
                    }
                }
                resultLock.unlock()
            }
            group.leave()
        }
        group.enter()
        self.json("https://api.frankfurter.app/latest?from=USD&to=CNY") { json in
            if let rate = Self.double((json?["rates"] as? [String: Any])?["CNY"]) {
                resultLock.lock(); exchange = DashboardQuote(price: rate, changePercent: 0); resultLock.unlock()
            }
            group.leave()
        }
        group.notify(queue: .global(qos: .utility)) { [weak self] in
            guard let self else { return }
            self.lock.lock()
            self.marketFetchInFlight = false
            self.lastMarketFetch = Date()
            if let btc = crypto["BTCUSDT"], let doge = crypto["DOGEUSDT"], let exchange {
                self.marketValue = DashboardMarkets(updatedAtMilliseconds: Self.nowMilliseconds,
                                                    btcUsdt: btc, dogeUsdt: doge, usdCny: exchange)
            } else {
                self.errorValue = "market_fetch_failed"
            }
            self.lock.unlock()
        }
    }

    private func finishWeather(_ weather: DashboardWeather?, error: String?) {
        self.lock.lock()
        self.weatherFetchInFlight = false
        self.lastWeatherFetch = Date()
        if let weather {
            self.weatherValue = weather
            self.errorValue = nil
            Store.shared.set(key: Key.cityName, value: weather.cityName)
            Store.shared.set(key: Key.cityCode, value: weather.cityCode)
        } else {
            self.errorValue = error
        }
        self.lock.unlock()
        DispatchQueue.main.async {
            NotificationCenter.default.post(name: .monitorServiceDidChange, object: self)
        }
    }

    private func json(_ endpoint: String, headers: [String: String] = [:],
                      completion: @escaping ([String: Any]?) -> Void) {
        guard let url = URL(string: endpoint) else { completion(nil); return }
        var request = URLRequest(url: url)
        headers.forEach { request.setValue($0.value, forHTTPHeaderField: $0.key) }
        self.session.dataTask(with: request) { data, _, error in
            guard error == nil, let data,
                  let object = try? JSONSerialization.jsonObject(with: data) else {
                completion(nil); return
            }
            if let dictionary = object as? [String: Any] { completion(dictionary) }
            else if let array = object as? [[String: Any]] { completion(["_array": array]) }
            else { completion(nil) }
        }.resume()
    }

    private static var nowMilliseconds: Int64 {
        Int64((Date().timeIntervalSince1970 * 1_000).rounded())
    }
    private static func escape(_ value: String) -> String {
        value.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? value
    }
    private static func double(_ value: Any?) -> Double? {
        if let value = value as? Double { return value }
        if let value = value as? Int { return Double(value) }
        if let value = value as? NSNumber { return value.doubleValue }
        if let value = value as? String { return Double(value) }
        return nil
    }
    private static func int(_ value: Any?) -> Int? { self.double(value).map { Int($0.rounded()) } }
    private static func hour(from value: String) -> Int {
        guard let separator = value.lastIndex(of: "T") else { return 0 }
        return Int(value[value.index(after: separator)...].prefix(2)) ?? 0
    }
    private static func nextHourlyIndex(times: [String]) -> Int {
        let formatter = DateFormatter(); formatter.dateFormat = "yyyy-MM-dd'T'HH:mm"
        return times.firstIndex { (formatter.date(from: $0) ?? .distantPast) > Date() } ?? 0
    }
    private static func temperature(_ object: [String: Any]) -> Int? {
        if let value = self.double(object["temperature"]) { return Int(value.rounded()) }
        if let nested = object["temperature"] as? [String: Any], let value = self.double(nested["value"]) {
            return Int(value.rounded())
        }
        return self.int(object["temp"])
    }
    private static func conditionCode(_ object: [String: Any]) -> Int? {
        if let nested = object["condition"] as? [String: Any] { return self.int(nested["code"]) }
        return self.int(object["icon"])
    }
    private static func qweatherDay(_ object: [String: Any]) -> DashboardWeatherDaily? {
        guard let high = self.int((object["temperatureMax"] as? [String: Any])?["value"]),
              let low = self.int((object["temperatureMin"] as? [String: Any])?["value"]),
              let daytime = object["daytime"] as? [String: Any],
              let code = self.conditionCode(daytime) else { return nil }
        let rawDate = (object["forecastStartTime"] as? String) ?? ""
        let iso = ISO8601DateFormatter().date(from: rawDate)
        let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX"); formatter.dateFormat = "EEE"
        return DashboardWeatherDaily(day: iso.map { formatter.string(from: $0).uppercased() } ?? "---",
                                     highC: high, lowC: low, weatherCode: code)
    }
    private static func qweatherHour(_ object: [String: Any]) -> DashboardWeatherHourly? {
        guard let time = object["forecastTime"] as? String,
              let temperature = self.temperature(object),
              let code = self.conditionCode(object) else { return nil }
        return DashboardWeatherHourly(hour: self.hour(from: time), temperatureC: temperature,
                                      weatherCode: code, isDay: true)
    }
    private static func amapWeatherCode(_ text: String) -> Int {
        if text.contains("雷") { return 95 }
        if text.contains("雪") || text.contains("雨夹雪") { return 71 }
        if text.contains("雨") { return 61 }
        if text.contains("雾") || text.contains("霾") || text.contains("沙") || text.contains("尘") { return 45 }
        if text.contains("云") || text.contains("阴") { return 3 }
        return 0
    }
    private static func displayCityName(_ value: String) -> String {
        if value.unicodeScalars.allSatisfy({ $0.value >= 32 && $0.value <= 126 }) {
            return String(value.prefix(31))
        }
        let latin = value.applyingTransform(.toLatin, reverse: false)?
            .applyingTransform(.stripDiacritics, reverse: false) ?? value
        let printable = latin.unicodeScalars.filter { $0.value >= 32 && $0.value <= 126 }
        return String(String.UnicodeScalarView(printable)).prefix(31).description
    }
}
