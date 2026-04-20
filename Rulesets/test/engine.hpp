#pragma once
#include "api.hpp"
#include "nlohmann/json.hpp"
#include <algorithm>
#include <random>

// Transparent wrapper around nlohmann::json.
// Inherit from this class to build engine-specific functionality.
class State : public nlohmann::json {
	std::mt19937 rng;
public:
	// Default constructor.
	State() = default;

	// Seed the internal RNG. Call this with entropy from the host engine
	// before any shuffle operations.
	void seed(uint32_t s) { rng.seed(s); }

	// Forwarding constructor from nlohmann::json.
	// Deliberately the only converting constructor — constructing a State
	// from a brace-enclosed initializer list therefore requires two
	// implicit conversions ({..}→json→State), which C++ forbids. This
	// leaves operator=(nlohmann::json) as the sole viable candidate for
	// assignments like: state = { {"key", value}, ... };
	State(nlohmann::json j) : nlohmann::json(std::move(j)) {}

	// Assignment from json. Only one implicit conversion needed ({..}→json),
	// so this wins unambiguously over the copy/move assignment operators.
	State& operator=(nlohmann::json other) {
		nlohmann::json::operator=(std::move(other));
		return *this;
	}

	// Move a card to a new zone, updating both the zone card lists and
	// the card's zone and position fields (centered in the new zone).
	void move_card(const std::string& card_id, const std::string& new_zone) {
		std::string old_zone = (*this)["cards"][card_id]["zone"];

		// Remove from old zone's card list
		auto& old_cards = (*this)["zones"][old_zone]["cards"];
		old_cards.erase(std::remove(old_cards.begin(), old_cards.end(), card_id), old_cards.end());

		// Add to new zone's card list
		(*this)["zones"][new_zone]["cards"].push_back(card_id);

		// Position the card: snap_point (relative to top_left) if defined,
		// otherwise the center of the zone.
		auto& zone = (*this)["zones"][new_zone];
		double cx, cy;
		if (zone.contains("snap_point")) {
			cx = zone["top_left"][0].get<double>() + zone["snap_point"][0].get<double>();
			cy = zone["top_left"][1].get<double>() + zone["snap_point"][1].get<double>();
		} else {
			cx = (zone["top_left"][0].get<double>() + zone["bottom_right"][0].get<double>()) / 2.0;
			cy = (zone["top_left"][1].get<double>() + zone["bottom_right"][1].get<double>()) / 2.0;
		}
		double z = (*this)["cards"][card_id]["position"][2].get<double>();
		(*this)["cards"][card_id]["position"] = {cx, cy, z};
		(*this)["cards"][card_id]["zone"] = new_zone;
	}

	// Set a card's shown_face to the image registered under face_key in its
	// faces dictionary. Does nothing if face_key is not present in faces.
	bool show_face(const std::string& card_id, const std::string& face_key) {
		auto& card = (*this)["cards"][card_id];
		if (card["faces"].contains(face_key)) {
			card["shown_face"] = card["faces"][face_key];
			return true;
		}
		return false;
	}

	// Randomly reorder the cards in a zone and assign each card a z value
	// equal to its new index, so stack order is reflected in the renderer.
	void shuffle_zone(const std::string& zone_name) {
		auto& cards = (*this)["zones"][zone_name]["cards"];

		std::shuffle(cards.begin(), cards.end(), rng);

		for (std::size_t i = 0; i < cards.size(); i++) {
			std::string card_id = cards[i];
			(*this)["cards"][card_id]["position"][2] = static_cast<int>(i);
		}
	}

	// Serialize to a Godot String for returning from sandbox functions.
	String serialize() const {
		return String(dump());
	}
};
