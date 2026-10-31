#pragma once
// La bandeja de entrada y su archivo. No depende del juego, así que el arnés también la usa.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct Mail {
	uint32_t id = 0;
	std::string from;
	std::string subject;
	std::string body;
	std::string received;
	bool read = false;

	// Las dos respuestas que ofrece quien envía el correo. Sin ellas, no se puede responder.
	std::string replyKey;
	std::string goodText;
	std::string badText;
	std::string goodScript;
	std::string badScript;
	int answer = 0;  // 0 sin responder, 1 buena, 2 mala
	std::string answeredAt;
};

class MailStore {
public:
	// El archivo de sesión refleja siempre la partida en curso; el helper lo lee de ahí.
	void SetSessionPath(std::filesystem::path path) { sessionPath_ = std::move(path); }

	void Clear();
	uint32_t Add(std::string from, std::string subject, std::string body, std::string received);
	uint32_t AddWithReplies(std::string from, std::string subject, std::string body, std::string received,
		std::string replyKey, std::string goodText, std::string badText, std::string goodScript, std::string badScript);

	// Devuelve true solo la primera vez: una respuesta no se puede cambiar. Si tiene script, lo deja en script.
	bool Answer(uint32_t id, int answer, std::string& key, std::string& script, std::string answeredAt = {});
	bool KnowsReplyKey(const std::string& key) const { return replies_.count(key) != 0; }
	const std::map<std::string, int>& Replies() const { return replies_; }
	bool MarkRead(uint32_t id);

	// Valores compartidos con mods y páginas: se guardan con la partida, como el correo.
	void SetValue(const std::string& key, const std::string& value);
	const std::map<std::string, std::string>& Values() const { return values_; }
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
	// Sobrevive a borrar el correo: el mod que lo envió sigue pudiendo preguntar.
	std::map<std::string, int> replies_;
	std::map<std::string, std::string> values_;
	uint32_t nextId_ = 1;
	bool dirty_ = true;
};
