#pragma once
// La bandeja de entrada y su archivo. No depende del juego, así que el arnés también la usa.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct Mail {
	uint32_t id = 0;
	std::string from;
	std::string subject;
	std::string body;
	std::string received;
	bool read = false;
};

class MailStore {
public:
	// El archivo de sesión refleja siempre la partida en curso; el helper lo lee de ahí.
	void SetSessionPath(std::filesystem::path path) { sessionPath_ = std::move(path); }

	void Clear();
	uint32_t Add(std::string from, std::string subject, std::string body, std::string received);
	bool MarkRead(uint32_t id);
	bool Remove(uint32_t id);

	int UnreadCount() const;
	const std::vector<Mail>& Mails() const { return mails_; }

	// Guarda en el archivo de sesión si hubo cambios. Si Windows lo tiene ocupado, se reintenta en la siguiente llamada.
	void FlushSession();

	// Partida guardada: una copia completa por cada archivo de partida del juego.
	bool SaveTo(const std::filesystem::path& path) const;
	// Un archivo dañado se aparta y no se carga: nunca debe impedir jugar.
	bool LoadFrom(const std::filesystem::path& path);

private:
	std::filesystem::path sessionPath_;
	std::vector<Mail> mails_;
	uint32_t nextId_ = 1;
	bool dirty_ = true;
};
