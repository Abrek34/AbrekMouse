// RawAccel Linux — sahte rawaccel daemon IPC sunucusu (AJ-H)
//
// `rawaccel-cli status` / `monitor` / `latency` komutlarının liveness
// mantığını, GERÇEK bir daemon olmadan test edebilmek için: UNIX sokete
// bağlanır, satır bazlı komutu okur ve "status" için geçerli bir status
// JSON'u döner.  Böylece "canlı daemon, PID dosyası yok" senaryosu
// (kurulum sırasında PID dosyasının silinip yeniden yazıldığı aralık)
// deterministik olarak üretilebilir.
//
// Kullanım: fake_ipc_server <socket_yolu> [cihaz_adi] [maks_istek]
//   soket yolu   — bağlanılacak UNIX soket
//   cihaz adı    — status JSON'unda görünecek sahte cihaz (varsayılan)
//   maks istek   — kaç bağlantıdan sonra çıkılacağı (varsayılan 32)
//
// Çıkış kodu: 0 = normal kapanış, 1 = soket kurulamadı.
// NOT: bu sunucu YALNIZCA test içindir; üretim kodunda kullanılmaz.

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "kullanım: %s <socket_yolu> [cihaz_adi] [maks_istek]\n", argv[0]);
        return 1;
    }
    const std::string sock_path = argv[1];
    const std::string device_name = (argc > 2) ? argv[2] : "SAHTE-CIHAZ";
    const int max_requests = (argc > 3) ? std::atoi(argv[3]) : 32;

    // ⚠️ SA_RESTART YOK: std::signal() glibc'de SA_RESTART kurar ve
    // kesilen accept() OTOMATİK olarak yeniden başlatılır — sunucu SIGTERM
    // aldığı halde accept() içinde sonsuza dek kalır ve testi kilitler.
    // sa_flags = 0 ⇒ accept() EINPT döner, döngü g_stop'u görüp çıkar.
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                     // SA_RESTART YOK
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT, &sa, nullptr);

    ::unlink(sock_path.c_str());
    int srv = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (srv < 0) { std::perror("socket"); return 1; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (sock_path.size() >= sizeof(addr.sun_path)) {
        std::fprintf(stderr, "socket yolu çok uzun\n");
        return 1;
    }
    std::strncpy(addr.sun_path, sock_path.c_str(), sizeof(addr.sun_path) - 1);
    if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }
    if (::listen(srv, 8) < 0) { std::perror("listen"); return 1; }

    // "Hazır" işareti: üst script sunucunun gerçekten dinlediğini bilmeli.
    std::printf("READY\n");
    std::fflush(stdout);

    std::string device_json = "\"" + device_name + "\"";
    int served = 0;
    while (!g_stop && served < max_requests) {
        int fd = ::accept(srv, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        char buf[4096];
        ssize_t n = ::recv(fd, buf, sizeof(buf) - 1, 0);
        std::string request;
        if (n > 0) request.assign(buf, static_cast<size_t>(n));
        std::string reply;
        if (request.rfind("status", 0) == 0) {
            reply = "{\"running\":true,\"active_profile\":\"sahte\",\"devices\":[{"
                    "\"name\":" + device_json + ",\"path\":\"/dev/input/event99\","
                    "\"device_id\":\"usb:SAHTE:000000000000\",\"dpi\":1600,"
                    "\"poll_rate\":1000,\"real_polling_rate\":1000}]}";
        } else {
            reply = "{\"ok\":true}";
        }
        (void)!::send(fd, reply.data(), reply.size(), MSG_NOSIGNAL);
        ::close(fd);
        ++served;
    }
    ::close(srv);
    ::unlink(sock_path.c_str());
    return 0;
}
