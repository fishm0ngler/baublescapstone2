#include "engine.hpp"
#include <string>
#include <vector>

static State state;

static const double FAN_OFFSET_Y = 30.0;

// ─── Predicates & utilities ───────────────────────────────────────────────────

// True if zone_name is one of the seven tableau columns.
static bool is_tableau_zone(const std::string& name)
{
	return name.rfind("tableau_", 0) == 0;
}

// True if zone_name is one of the four foundation piles.
static bool is_foundation_zone(const std::string& name)
{
	return name.rfind("foundation_", 0) == 0;
}

// Returns the top card id of a zone (back of its cards array), or "" if the
// zone doesn't exist or is empty.
static std::string zone_top(const std::string& zone)
{
	if (!state["zones"].contains(zone)) return "";
	auto& cards = state["zones"][zone]["cards"];
	if (cards.empty()) return "";
	return cards.back().get<std::string>();
}

// Returns the zone name that a card currently belongs to, or "" if the card
// doesn't exist in state.
static std::string card_zone(const std::string& card_id)
{
	if (!state["cards"].contains(card_id)) return "";
	return state["cards"][card_id]["zone"].get<std::string>();
}

// Looks at the previous entry in meta.history. If it was a click on a card
// (i.e. the target exists in state["cards"]), returns that card id.
// Returns "" if there is no previous action or it was not a card click.
static std::string get_prev_card()
{
	auto& history = state["meta"]["history"];
	if (history.size() < 2) return "";
	auto& prev = history[history.size() - 2];
	std::string prev_action = prev[0].get<std::string>();
	std::string prev_target = prev[1].get<std::string>();
	if (prev_action == "click" && state["cards"].contains(prev_target))
		return prev_target;
	return "";
}

// Returns the numeric rank of a card (ace=1, 2=2, ... king=13), or -1 if unknown.
static int rank_value(const std::string& rank)
{
	const std::vector<std::string> ranks = {
		"ace","2","3","4","5","6","7","8","9","10","jack","queen","king"
	};
	for (int i = 0; i < (int)ranks.size(); i++)
		if (ranks[i] == rank) return i + 1;
	return -1;
}

// Returns true if card_id can legally be placed on top of to_zone as per
// Klondike tableau rules: kings on empty columns, otherwise one rank lower
// and opposite color.
static bool can_place_on_tableau(const std::string& card_id, const std::string& to_zone)
{
	auto& card_props  = state["cards"][card_id]["properties"];
	std::string rank  = card_props["rank"].get<std::string>();
	std::string color = card_props["color"].get<std::string>();

	auto& to_cards = state["zones"][to_zone]["cards"];
	if (to_cards.empty())
		return rank == "king";

	std::string top_id       = zone_top(to_zone);
	auto& top_props          = state["cards"][top_id]["properties"];
	std::string top_rank     = top_props["rank"].get<std::string>();
	std::string top_color    = top_props["color"].get<std::string>();

	return rank_value(rank) == rank_value(top_rank) - 1 && color != top_color;
}

// Returns true if card_id can legally be placed on to_zone as per Klondike
// foundation rules: suit must match the pile, aces start empty piles, and
// each subsequent card must be one rank higher.
static bool can_place_on_foundation(const std::string& card_id, const std::string& to_zone)
{
	auto& card_props  = state["cards"][card_id]["properties"];
	std::string rank  = card_props["rank"].get<std::string>();
	std::string suit  = card_props["suit"].get<std::string>();

	// Foundation zone names are "foundation_<suit>".
	if (to_zone != "foundation_" + suit) return false;

	auto& to_cards = state["zones"][to_zone]["cards"];
	if (to_cards.empty())
		return rank == "ace";

	std::string top_rank =
		state["cards"][zone_top(to_zone)]["properties"]["rank"].get<std::string>();
	return rank_value(rank) == rank_value(top_rank) + 1;
}

// ─── Move stubs ───────────────────────────────────────────────────────────────

// Draw the top card of stock face-up onto the waste pile.
// If the stock is empty, recycle the entire waste pile back into the stock
// face-down (standard Klondike allows this unlimited times).
static void draw_to_waste()
{
	auto& stock_cards = state["zones"]["stock"]["cards"];

	if (stock_cards.empty()) {
		// Recycle: move all waste cards back to stock in reverse order so that
		// the card that was on top of the waste ends up on top of the stock.
		auto& waste_cards = state["zones"]["waste"]["cards"];
		while (!waste_cards.empty()) {
			std::string card_id = waste_cards.back().get<std::string>();
			state.move_card(card_id, "stock");
			state.show_face(card_id, "back");
		}
		// Re-index z so the stack order is reflected correctly.
		for (std::size_t i = 0; i < stock_cards.size(); i++)
			state["cards"][stock_cards[i]]["position"][2] = static_cast<int>(i);
		return;
	}

	// Normal draw: flip the top stock card face-up onto waste.
	print("draw_to_waste");
	std::string card_id = stock_cards.back();
	state.move_card(card_id, "waste");
	state.show_face(card_id, "front");

	// Place on top of the waste stack visually.
	auto& waste_cards = state["zones"]["waste"]["cards"];
	state["cards"][card_id]["position"][2] = static_cast<int>(waste_cards.size() - 1);
}

// Move the top card of waste onto a tableau column, if the move is legal.
static void waste_to_tableau(const std::string& to_zone)
{
	std::string card_id = zone_top("waste");
	if (card_id.empty()) return;
	if (!can_place_on_tableau(card_id, to_zone)) return;

	print("waste_to_tableau");
	state.move_card(card_id, to_zone);
	state["cards"][card_id]["position"][2] =
		static_cast<int>(state["zones"][to_zone]["cards"].size() - 1);
	state["zones"][to_zone]["snap_point"][1] =
		state["zones"][to_zone]["snap_point"][1].get<double>() + FAN_OFFSET_Y;
}

// Move the top card of waste onto a foundation pile, if the move is legal.
static void waste_to_foundation(const std::string& to_zone)
{
	std::string card_id = zone_top("waste");
	if (card_id.empty()) return;
	if (!can_place_on_foundation(card_id, to_zone)) return;

	print("waste_to_foundation");
	state.move_card(card_id, to_zone);
	state["cards"][card_id]["position"][2] =
		static_cast<int>(state["zones"][to_zone]["cards"].size() - 1);
}

// Move the sub-stack starting at from_card (and everything above it in its
// column) onto to_zone. In Klondike any face-up sequence may move as a unit.
static void tableau_to_tableau(const std::string& from_card, const std::string& to_zone)
{
	std::string from_zone = card_zone(from_card);

	// The bottom card of the sub-stack must satisfy the tableau placement rule.
	if (!can_place_on_tableau(from_card, to_zone)) return;

	print("tableau_to_tableau");
	// Collect the sub-stack: from_card through to the top of the column.
	// We snapshot it before moving so the loop isn't affected by move_card
	// modifying the underlying cards array.
	auto& from_cards = state["zones"][from_zone]["cards"];
	std::vector<std::string> sub_stack;
	bool found = false;
	for (auto& entry : from_cards) {
		std::string id = entry.get<std::string>();
		if (id == from_card) found = true;
		if (found) sub_stack.push_back(id);
	}
	if (sub_stack.empty()) return;

	// Move each card in order (bottom of sub-stack first) onto to_zone.
	for (const std::string& card_id : sub_stack) {
		state.move_card(card_id, to_zone);
		state["cards"][card_id]["position"][2] =
			static_cast<int>(state["zones"][to_zone]["cards"].size() - 1);
		state["zones"][to_zone]["snap_point"][1] =
			state["zones"][to_zone]["snap_point"][1].get<double>() + FAN_OFFSET_Y;
	}

	// Retreat from_zone's snap_point by one step per card removed.
	state["zones"][from_zone]["snap_point"][1] =
		state["zones"][from_zone]["snap_point"][1].get<double>()
		- static_cast<double>(sub_stack.size()) * FAN_OFFSET_Y;

	// Reveal the card now on top of from_zone, if any.
	std::string new_top = zone_top(from_zone);
	if (!new_top.empty())
		state.show_face(new_top, "front");
}

// Move only the single top card of from_zone onto a foundation pile.
// (Sequences cannot be moved to foundations.)
static void tableau_to_foundation(const std::string& from_zone, const std::string& to_zone)
{
	std::string card_id = zone_top(from_zone);
	if (card_id.empty()) return;
	if (!can_place_on_foundation(card_id, to_zone)) return;

	print("tableau_to_foundation");
	state.move_card(card_id, to_zone);
	state["cards"][card_id]["position"][2] =
		static_cast<int>(state["zones"][to_zone]["cards"].size() - 1);

	// Retreat from_zone's snap_point now that a card has been removed.
	state["zones"][from_zone]["snap_point"][1] =
		state["zones"][from_zone]["snap_point"][1].get<double>() - FAN_OFFSET_Y;

	// Reveal the card now on top of from_zone, if any.
	std::string new_top = zone_top(from_zone);
	if (!new_top.empty())
		state.show_face(new_top, "front");
}

static Variant sandbox_init(int seed)
{
	state.seed(static_cast<uint32_t>(seed));
	const std::vector<std::string> suits = {"spades", "hearts", "diamonds", "clubs"};
	const std::vector<std::string> ranks = {"ace", "2", "3", "4", "5", "6", "7",
											 "8", "9", "10", "jack", "queen", "king"};

	state = {
		{"cards", State::object()},
		{"zones", {
			{"stock", {
				{"cards", State::array()},
				{"top_left",     {20,  20}},
				{"bottom_right", {170, 160}},
			}},
			{"waste", {
				{"cards", State::array()},
				{"top_left",     {180, 20}},
				{"bottom_right", {330, 160}},
			}},
			{"foundation_spades", {
				{"cards", State::array()},
				{"top_left",     {500, 20}},
				{"bottom_right", {650, 160}},
			}},
			{"foundation_hearts", {
				{"cards", State::array()},
				{"top_left",     {660, 20}},
				{"bottom_right", {810, 160}},
			}},
			{"foundation_diamonds", {
				{"cards", State::array()},
				{"top_left",     {820, 20}},
				{"bottom_right", {970, 160}},
			}},
			{"foundation_clubs", {
				{"cards", State::array()},
				{"top_left",     {980, 20}},
				{"bottom_right", {1130, 160}},
			}},
			{"tableau_1", {
				{"cards", State::array()},
				{"top_left",     {20,  200}},
				{"bottom_right", {170, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_2", {
				{"cards", State::array()},
				{"top_left",     {180, 200}},
				{"bottom_right", {330, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_3", {
				{"cards", State::array()},
				{"top_left",     {340, 200}},
				{"bottom_right", {490, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_4", {
				{"cards", State::array()},
				{"top_left",     {500, 200}},
				{"bottom_right", {650, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_5", {
				{"cards", State::array()},
				{"top_left",     {660, 200}},
				{"bottom_right", {810, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_6", {
				{"cards", State::array()},
				{"top_left",     {820, 200}},
				{"bottom_right", {970, 680}},
				{"snap_point",   {75, 75}},
			}},
			{"tableau_7", {
				{"cards", State::array()},
				{"top_left",     {980, 200}},
				{"bottom_right", {1130, 680}},
				{"snap_point",   {75, 75}},
			}}
		}},
		{"meta", {
			{"history", State::array()}
		}},
		{"properties", State::object()},
		{"ui", State::object()}
	};

	// Create all 52 cards face-down in stock
	for (const auto& suit : suits) {
		for (const auto& rank : ranks) {
			std::string id = rank + "_of_" + suit;
			state["cards"][id] = {
				{"shown_face", "card_back"},
				{"faces", {
					{"front", id},
					{"back",  "card_back"}
				}},
				{"position",   {0, 0, 0}},
				{"scale",      {1.0, 1.0}},
				{"zone",       "stock"},
				{"properties", {
					{"rank",  rank},
					{"suit",  suit},
					{"color", (suit == "hearts" || suit == "diamonds") ? "red" : "black"}
				}}
			};
			state.move_card(id, "stock");
		}
	}

	// Shuffle the stock
	state.shuffle_zone("stock");

	
	// Deal to tableau: column n gets n+1 cards, only the top card is face-up.
	for (int col = 0; col < 7; col++) {
		std::string zone_name = "tableau_" + std::to_string(col + 1);

		for (int row = 0; row <= col; row++) {
			std::string card_id = state["zones"]["stock"]["cards"].back();
			state.move_card(card_id, zone_name);
			state["cards"][card_id]["position"][2] = row;
			state["zones"][zone_name]["snap_point"][1] =
				state["zones"][zone_name]["snap_point"][1].get<double>() + FAN_OFFSET_Y;

			if (row == col)
				state.show_face(card_id, "front");
		}
	}

	// Re-index z values for remaining stock cards
	auto& stock_cards = state["zones"]["stock"]["cards"];
	for (std::size_t i = 0; i < stock_cards.size(); i++)
		state["cards"][stock_cards[i]]["position"][2] = static_cast<int>(i);

	return state.serialize();
}

static Variant sandbox_update(String action, String target)
{
	std::string act(action);
	std::string tgt(target);

	// Record this action before dispatching so get_prev_card() can read it.
	state["meta"]["history"].push_back({act, tgt});

	if (act == "click") {

		// Resolve the effective zone: if the target is a card, use its zone;
		// otherwise the target is already a zone name.
		std::string tgt_zone = state["cards"].contains(tgt) ? card_zone(tgt) : tgt;

		print(tgt);

		// Clicking the stock zone or any card in the stock draws the top card
		// face-up onto the waste pile.
		if (tgt_zone == "stock") {
			draw_to_waste();
		}

		// Clicking a tableau zone (or a card within one) attempts to move the
		// previously clicked card or stack onto that column.
		else if (is_tableau_zone(tgt_zone)) {
			std::string prev = get_prev_card();
			if (!prev.empty()) {
				std::string from = card_zone(prev);
				if (from == "waste" && prev == zone_top("waste"))
					waste_to_tableau(tgt_zone);
				else if (is_tableau_zone(from))
					tableau_to_tableau(prev, tgt_zone);
			}
		}

		// Clicking a foundation zone (or a card within one) attempts to move
		// the previously clicked card onto that pile.
		// Only single cards may go to foundations.
		else if (is_foundation_zone(tgt_zone)) {
			std::string prev = get_prev_card();
			if (!prev.empty()) {
				std::string from = card_zone(prev);
				if (from == "waste" && prev == zone_top("waste"))
					waste_to_foundation(tgt_zone);
				else if (is_tableau_zone(from) && prev == zone_top(from))
					tableau_to_foundation(from, tgt_zone);
			}
		}

		// Clicking a card or any other target is treated as a selection gesture;
		// no immediate action is taken — it will be read as get_prev_card() on
		// the next relevant click.
	}

	return state.serialize();
}

int main() {
	ADD_API_FUNCTION(sandbox_init, "Variant", "Variant");
	ADD_API_FUNCTION(sandbox_update, "Variant", "Variant", "Variant");
}
