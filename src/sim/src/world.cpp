#include "railmaster/sim/world.hpp"

#include "railmaster/sim/freight.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace railmaster::sim {

namespace {

Ledger revenue_line(const CargoType& c) {
    if (c.key == "passengers") return Ledger::PassengerRevenue;
    if (c.key == "mail") return Ledger::MailRevenue;
    if (c.key == "troops") return Ledger::TroopRevenue;
    return Ledger::FreightRevenue;
}

std::int64_t fuel_per_km(const Balance::Finance& f, const LocomotiveType& loco, std::size_t cars) {
    std::int64_t base = f.fuel_per_km_steam;
    if (loco.fuel == Fuel::Diesel) base = f.fuel_per_km_diesel;
    if (loco.fuel == Fuel::Electric) base = f.fuel_per_km_electric;
    return base + f.fuel_per_km_per_car * static_cast<std::int64_t>(cars);
}

} // namespace

World::World(const WorldConfig& config, GameData data)
    : rng_(config.seed),
      date_(config.start_date),
      terrain_(config.width_tiles, config.height_tiles, config.tile_size_m),
      data_(std::move(data)),
      economy_(config.width_tiles, config.height_tiles, std::int64_t{config.tile_size_m} * 1000, data_.cargo,
               data_.balance),
      railway_(config.seed),
      sandbox_(config.sandbox),
      difficulty_(config.difficulty),
      business_cycle_(config.business_cycle) {
    Investor you;
    you.name = "You";
    you.cash = Money::dollars(data_.balance.stock.founder_fortune);
    market_.investors.push_back(std::move(you));
    if (config.starting_cash) {
        const Money capital = Money::dollars(*config.starting_cash);
        found_for(kHumanPlayer, {"Railmaster Railroad", capital.scaled(1, 2), capital - capital.scaled(1, 2)});
    } else if (config.found_player_company) {
        const FoundCompany terms = config.founding.value_or(
            FoundCompany{"Railmaster Railroad", Money::dollars(data_.balance.stock.founder_investment),
                         Money::dollars(data_.balance.stock.outside_investment)});
        if (const auto why = founding_problem(kHumanPlayer, terms)) throw std::invalid_argument("founding: " + *why);
        found_for(kHumanPlayer, terms);
    }
    rival_ai_ = config.rival_ai;
    chairman_can_be_fired_ = config.chairman_can_be_fired;
    chairman_can_resign_ = config.chairman_can_resign;
    town_growth_ = config.town_growth;
    industries_appear_ = config.industries_appear;
    terrain_.generate_rolling_hills(rng_, data_.balance.map.max_height_m);
    refresh_economy_terrain();
    if (config.populate && !data_.industries.all().empty()) {
        populate_economy(economy_, terrain_, data_.cargo, data_.industries, rng_, date_.year(), data_.balance);
        // History, so the map starts with prices and cargo in place.
        economy_.settle(data_.cargo, data_.industries, date_.year(), data_.balance.economy.history_days);
        // Industries start with a year of accounts, so they have a price.
        economy_.close_accounts(data_.cargo, data_.industries, data_.balance.industries, 12);
    }
    railway_.set_balance(data_.balance);
    railway_.set_rules({.breakdowns = !config.sandbox});

    // Rivals: a random choice of the tycoons, each founding a company.
    std::vector<std::size_t> pool(data_.tycoons.all().size());
    for (std::size_t i = 0; i < pool.size(); ++i) pool[i] = i;
    const auto wanted = std::min<std::size_t>(pool.size(), static_cast<std::size_t>(std::max(0, config.rivals)));
    for (std::size_t i = 0; i < wanted; ++i) {
        const std::size_t pick = i + rng_.below(static_cast<std::uint32_t>(pool.size() - i));
        std::swap(pool[i], pool[pick]);
        const Tycoon& t = data_.tycoons.all()[pool[i]];
        rivals_.push_back({add_player_company(t.company, t.name), pool[i], -1'000'000, {}});
    }
}

PlayerId World::add_player_company(std::string company_name, std::string chairman) {
    const auto who = static_cast<PlayerId>(market_.investors.size());
    Investor inv;
    inv.name = std::move(chairman);
    inv.cash = Money::dollars(data_.balance.stock.founder_fortune);
    market_.investors.push_back(std::move(inv));
    found_for(who, {std::move(company_name), Money::dollars(data_.balance.stock.founder_investment),
                    Money::dollars(data_.balance.stock.outside_investment)});
    return who;
}

std::optional<std::string> World::founding_problem(PlayerId who, const FoundCompany& t) const {
    const Balance::Stock& b = data_.balance.stock;
    const Investor& inv = market_.investors.at(who);
    if (inv.chairs) return std::string("you already run a company");
    if (t.personal_investment < Money::dollars(b.min_founder_investment)) {
        return "you must put in at least " + std::to_string(b.min_founder_investment) + " dollars";
    }
    if (t.personal_investment > inv.cash) return std::string("you do not have that much money");
    if (t.outside_investment < Money{}) return std::string("outside investment cannot be negative");
    if (t.outside_investment > Money::dollars(b.outside_investment)) {
        return "outside investors offer at most " + std::to_string(b.outside_investment) + " dollars";
    }
    return std::nullopt;
}

CompanyId World::found_for(PlayerId who, const FoundCompany& t) {
    const auto id = static_cast<CompanyId>(market_.companies.size());
    Investor& inv = market_.investors.at(who);
    std::string name = t.name.empty() ? inv.name + " Railroad" : t.name;
    market_.companies.emplace_back(std::move(name), t.personal_investment + t.outside_investment, date_.year(),
                                   data_.balance, id);
    market_.companies.back().set_economic_state(economic_state_);
    // The founder's money buys shares at the founding price; the rest are the outside investors'.
    inv.cash -= t.personal_investment;
    inv.add_shares(id, t.personal_investment.in_cents() /
                           std::max<std::int64_t>(1, data_.balance.stock.founding_share_price_cents));
    inv.chairs = id;
    if (who == kHumanPlayer) note_player_company();
    return id;
}

void World::tick() {
    ++total_ticks_;
    railway_.set_today(date_.days_since_epoch());
    railway_.tick(data_.locomotives);
    for (TrainId id : railway_.take_crashes()) {
        const Train& t = railway_.train(id);
        company(t.owner).write_off_train(data_.locomotives.get(t.loco).cost);
    }
    for (const auto& [train, station] : railway_.take_arrivals()) {
        const Earnings e = handle_arrival(railway_, economy_, data_.cargo, data_.industries, train, station,
                                          date_.days_since_epoch(), total_ticks_, revenue_permille(station));
        Train& t = railway_.train_mut(train);
        Company& runner = company(t.owner);
        if (t.owner == company().id()) earned_ += e.total;
        for (const auto& [cargo, amount] : e.by_cargo) runner.post(revenue_line(data_.cargo.get(cargo)), amount);
        // Service to a town makes it grow.
        if (const auto town = railway_.station(station).town) {
            Town& tw = economy_.town_mut(*town);
            for (const auto& [cargo, amount] : e.by_cargo) {
                (data_.cargo.get(cargo).cargo_class == CargoClass::Express ? tw.express_this_month : tw.freight_this_month) +=
                    amount;
            }
        }
        pay_trackage(t, e.total);
    }
    if (++tick_of_day_ < kTicksPerDay) return;
    tick_of_day_ = 0;

    const CalendarDate before = date_.calendar();
    date_ += 1;
    const CalendarDate after = date_.calendar();

    on_new_day();
    if (after.month != before.month) on_new_month();
    if (after.year != before.year) on_new_year();
}


// The track's owners get the share of a stop's income matching the share of
// the leg run on their track; the runner still pays all its fuel [D].
void World::pay_trackage(Train& t, Money income) {
    std::int64_t total = 0;
    for (std::int64_t mm : t.leg_mm_by_owner) total += mm;
    if (total > 0 && income > Money{}) {
        for (std::size_t o = 0; o < t.leg_mm_by_owner.size(); ++o) {
            const auto owner = static_cast<CompanyId>(o);
            if (owner == t.owner || t.leg_mm_by_owner[o] == 0 || o >= market_.companies.size()) continue;
            const Money share = income.scaled(t.leg_mm_by_owner[o], total);
            company(owner).post(Ledger::TrackageIncome, share);
            company(t.owner).post(Ledger::TrackagePaid, share);
        }
    }
    t.leg_mm_by_owner.clear();
}

void World::refresh_economy_terrain() {
    economy_.set_terrain(terrain_);
    economy_terrain_revision_ = terrain_.revision();
}

std::int32_t World::cost_percent() const {
    return data_.balance.economic_states.cost_percent[index_of(economic_state())];
}

void World::set_economic_state(EconomicState s) {
    if (s != economic_state_) economy_news_ = s;
    economic_state_ = s;
    for (Company& c : market_.companies) c.set_economic_state(s);
    economy_.set_activity_percent(data_.balance.economic_states.activity_percent[index_of(s)]);
}

std::optional<EconomicState> World::take_economy_news() { return std::exchange(economy_news_, std::nullopt); }

std::int32_t World::revenue_permille(StationId s) const {
    const Station& st = railway_.station(s);
    const std::int32_t today = date_.days_since_epoch();
    std::int32_t age_permille;
    if (st.town) {
        const auto& first = economy_.towns()[*st.town].first_station_day;
        age_permille = station_age_permille(today - first.value_or(st.built_day), false);
    } else {
        age_permille = station_age_permille(today - st.built_day, true);
    }
    return difficulty_revenue_permille(difficulty_) * age_permille / 1000;
}

void World::charge_running_costs() {
    const std::int32_t today = date_.days_since_epoch();
    const Balance::Finance& f = data_.balance.finance;
    const std::size_t n = market_.companies.size();
    std::vector<Money> maintenance(n), fuel(n);
    for (const Train& t : railway_.trains()) {
        if (t.state == TrainState::Crashed) continue;
        const LocomotiveType& loco = data_.locomotives.get(t.loco);
        maintenance[t.owner] += annual_maintenance(loco, (today - t.built_day) / 365, t.oil, data_.balance).scaled(1, 12);
        const std::int64_t run_mm = t.distance_mm - t.fuel_billed_mm;
        fuel[t.owner] += Money::dollars(fuel_per_km(f, loco, t.cars.size())).scaled(run_mm, 1'000'000);
        railway_.train_mut(t.id).fuel_billed_mm = t.distance_mm;
    }
    for (Company& c : market_.companies) {
        if (c.defunct()) continue;
        // Easy games cut the player's maintenance, fuel and track costs [D];
        // by how much is [I]. The economic state moves fuel, labour and upkeep [C].
        const bool easy = difficulty_ == Difficulty::Easy && c.id() == company().id();
        const std::int64_t cost_pct = (easy ? f.easy_cost_percent : 100) * cost_percent() / 100;
        c.post(Ledger::TrainMaintenance, maintenance[c.id()].scaled(cost_pct, 100));
        c.post(Ledger::Fuel, fuel[c.id()].scaled(cost_pct, 100));
        c.post(Ledger::TrackUpkeep, c.track_value().scaled(f.track_upkeep_per_mille_month * cost_pct, 1000 * 100));
        c.post(Ledger::BuildingUpkeep,
               c.building_value().scaled(f.building_upkeep_per_mille_month * std::int64_t{cost_percent()}, 1000 * 100));
    }
}

void World::on_new_day() {
    if (terrain_.revision() != economy_terrain_revision_) refresh_economy_terrain();
    economy_.step_day(data_.cargo, data_.industries, date_.year());
    gather_at_stations(railway_, economy_, data_.cargo, data_.industries, date_.year());
}

// Running costs for the month just ended are charged on the 1st, before a
// new year's accounts are opened, so December's land in the old year.
void World::on_new_month() {
    start_new_month(railway_);
    charge_running_costs();
    account_industries(1);
    pay_station_buildings();
    grow_towns();
    for (Company& c : market_.companies) c.record_month();
    const std::int32_t checks = data_.balance.economic_states.checks_per_year;
    if (business_cycle_ && checks > 0 && (date_.month() - 1) % std::max(1, 12 / checks) == 0) {
        set_economic_state(next_economic_state(economic_state(), rng_, data_.balance.economic_states));
    }
    last_forced_sale_ = monthly_market(market_)[kHumanPlayer];
    for (const auto& [id, ratio] : apply_splits(market_)) {
        news_.push_back(company(id).name() + " shares split " + std::to_string(ratio) + " for 1");
    }
    // Bond interest [D] and dividends are paid at the end of each quarter.
    if ((date_.month() - 1) % 3 == 0) {
        for (Company& c : market_.companies) {
            if (c.defunct()) continue;
            c.charge_interest(3);
            pay_dividends(market_, c.id());
        }
    }
    run_rivals();
}

void World::account_industries(std::int32_t months) {
    const auto accounts = economy_.close_accounts(data_.cargo, data_.industries, data_.balance.industries, months);
    for (const Site& s : economy_.sites()) {
        if (!s.owner || company(*s.owner).defunct()) continue;
        Company& c = company(*s.owner);
        c.post(Ledger::IndustryIncome, accounts[s.id].revenue);
        c.post(Ledger::IndustryCosts, accounts[s.id].costs);
    }
}

// Each station's trade is a fixed market for each kind of building: those in
// range split it, the nearest taking most, by 1 / distance squared [D/C, I].
void World::pay_station_buildings() {
    const Balance::Stations& b = data_.balance.stations;
    const auto& buildings = railway_.station_buildings();
    for (StationId sid = 0; sid < railway_.stations().size(); ++sid) {
        Station& st = railway_.station_mut(sid);
        const MapPoint at = railway_.track().node(st.node).pos;
        const auto market = [&](StationBuildingType type) -> Money {
            switch (type) {
            case StationBuildingType::Hotel:
                return Money::dollars(b.hotel_per_load_day).scaled(st.passengers_waiting_milli_days, kMilli);
            case StationBuildingType::Restaurant:
                return Money::dollars(b.restaurant_per_load)
                    .scaled(st.passengers_boarded_milli + st.passengers_arrived_milli, kMilli);
            case StationBuildingType::Tavern:
                return Money::dollars(b.tavern_per_load).scaled(st.passengers_boarded_milli, kMilli);
            case StationBuildingType::PostOffice: return Money{};
            }
            return Money{};
        };
        for (const auto type : {StationBuildingType::Hotel, StationBuildingType::Restaurant, StationBuildingType::Tavern}) {
            const Money total = market(type);
            if (total <= Money{}) continue;
            // Weights 1 / (d^2 + 50 m^2), d in metres, so a building on the spot is not infinite.
            std::vector<std::pair<CompanyId, std::int64_t>> weights;
            std::int64_t sum = 0;
            for (const StationBuilding& sb : buildings) {
                if (sb.type != type) continue;
                const std::int64_t d = distance_mm(sb.pos, at);
                if (d > b.building_range_mm) continue;
                const std::int64_t m = d / 1000;
                const std::int64_t w = 1'000'000'000'000LL / (m * m + 2'500);
                weights.emplace_back(sb.owner, w);
                sum += w;
            }
            for (const auto& [owner, w] : weights) {
                if (owner < market_.companies.size() && !company(owner).defunct()) {
                    company(owner).post(Ledger::StationBuildingIncome, total.scaled(w, sum));
                }
            }
        }
        st.passengers_boarded_milli = st.passengers_arrived_milli = st.passengers_waiting_milli_days = 0;
    }
}

bool World::town_connected(std::size_t t) const {
    for (const Train& tr : railway_.trains()) {
        if (tr.state == TrainState::Crashed) continue;
        for (const StationId s : tr.route)
            if (railway_.station(s).town == t) return true;
    }
    return false;
}

void World::grow_towns() {
    const Balance::Towns& b = data_.balance.towns;
    for (std::size_t t = 0; t < economy_.towns().size(); ++t) {
        Town& tw = economy_.town_mut(t);
        const auto keep = [](std::vector<Money>& months, Money& now) {
            months.push_back(std::exchange(now, Money{}));
            if (months.size() > 12) months.erase(months.begin());
        };
        keep(tw.express_months, tw.express_this_month);
        keep(tw.freight_months, tw.freight_this_month);
        if (!town_growth_) continue;
        const std::int64_t houses = economy_.town_houses(t);
        if (houses <= 0) continue;
        // A year's income, annualised from the months known so far.
        Money express, freight;
        for (const Money& m : tw.express_months) express += m;
        for (const Money& m : tw.freight_months) freight += m;
        const auto months = static_cast<std::int64_t>(tw.express_months.size());
        express = express.scaled(12, months);
        freight = freight.scaled(12, months);
        std::int64_t rate = b.base_growth_permille +
                            express.whole_dollars() * b.express_permille_per_k_house / (1000 * houses) +
                            freight.whole_dollars() * b.freight_permille_per_k_house / (1000 * houses);
        rate = std::min<std::int64_t>(rate, b.max_growth_permille);
        if (!town_connected(t)) rate = rate * b.unconnected_permille / 1000;
        // Houses a year = houses x rate / 1000; a twelfth of that each month, in thousandths.
        tw.growth_milli += static_cast<std::int32_t>(houses * rate / 12);
        while (economy_.town_mut(t).growth_milli >= 1000) {
            economy_.town_mut(t).growth_milli -= 1000;
            add_house(t);
        }
    }
}

// A new house goes where the town is busiest: as near its stations as
// possible (else its centre), in an existing house cell with room or a
// free cell next to one [I: "near existing houses and stations"].
void World::add_house(std::size_t t) {
    const Balance::Towns& b = data_.balance.towns;
    const auto house_type = data_.industries.find("house");
    if (!house_type) return;
    const Town& tw = economy_.towns()[t];
    std::vector<std::pair<std::int32_t, std::int32_t>> focus;
    for (const Station& s : railway_.stations()) {
        if (s.town != t) continue;
        const MapPoint p = railway_.track().node(s.node).pos;
        focus.emplace_back(economy_.cell_x(p), economy_.cell_y(p));
    }
    if (focus.empty()) focus.emplace_back(tw.cx, tw.cy);
    const auto score = [&](std::int32_t x, std::int32_t y) {
        std::int32_t best = 1 << 20;
        for (const auto& [fx, fy] : focus) best = std::min(best, std::max(std::abs(fx - x), std::abs(fy - y)));
        return best * 64 + std::max(std::abs(tw.cx - x), std::abs(tw.cy - y));
    };
    std::vector<bool> occupied(static_cast<std::size_t>(economy_.width() * economy_.height()), false);
    for (const Site& s : economy_.sites()) {
        if (!s.closed) occupied[static_cast<std::size_t>(s.cy * economy_.width() + s.cx)] = true;
    }
    struct Option {
        std::int32_t score;
        std::int32_t x, y;
        std::optional<SiteId> existing;
    };
    std::vector<Option> options;
    for (const SiteId id : tw.houses) {
        const Site& h = economy_.sites()[id];
        if (h.level < b.max_houses_per_cell) options.push_back({score(h.cx, h.cy), h.cx, h.cy, id});
        for (std::int32_t dy = -1; dy <= 1; ++dy) {
            for (std::int32_t dx = -1; dx <= 1; ++dx) {
                const std::int32_t x = h.cx + dx, y = h.cy + dy;
                if (x < 0 || y < 0 || x >= economy_.width() || y >= economy_.height()) continue;
                if (occupied[static_cast<std::size_t>(y * economy_.width() + x)]) continue;
                if (terrain_.ground(x, y) == GroundType::Water) continue;
                if (std::max(std::abs(tw.cx - x), std::abs(tw.cy - y)) > b.max_radius_cells) continue;
                options.push_back({score(x, y), x, y, std::nullopt});
            }
        }
    }
    if (options.empty()) return;
    std::int32_t best = options.front().score;
    for (const Option& o : options) best = std::min(best, o.score);
    std::vector<const Option*> top;
    for (const Option& o : options)
        if (o.score == best) top.push_back(&o);
    const Option& pick = *top[rng_.below(static_cast<std::uint32_t>(top.size()))];
    if (pick.existing) {
        ++economy_.site_mut(*pick.existing).level;
    } else {
        const SiteId id = economy_.add_site(data_.industries, *house_type, pick.x, pick.y);
        economy_.town_mut(t).houses.push_back(id);
    }
}

void World::note_player_company() {
    if (const auto c = player_company()) last_player_company_ = *c;
}

void World::review_chairmen() {
    const Balance::Corporate& b = data_.balance.corporate;
    for (const Company& c : market_.companies) {
        if (c.defunct()) continue;
        std::optional<PlayerId> chairman;
        for (std::size_t i = 0; i < market_.investors.size(); ++i) {
            if (market_.investors[i].chairs == c.id()) chairman = static_cast<PlayerId>(i);
        }
        const std::int32_t streak = c.bad_year_streak();
        if (!chairman || streak < b.grumble_after_bad_years) continue;
        const std::string who = market_.investors[*chairman].name;
        if (streak < b.oust_after_bad_years || !chairman_can_be_fired_) {
            news_.push_back("Investors in " + c.name() + " are grumbling after " + std::to_string(streak) +
                            " bad years under " + who);
            continue;
        }
        // The other shareholders vote the chairman out; a chairman holding
        // over half the shares cannot be removed [C].
        if (market_.investors[*chairman].shares_in(c.id()) * 2 > c.shares_outstanding()) {
            news_.push_back(who + " keeps the chair of " + c.name() + " with a majority of its shares");
            continue;
        }
        market_.investors[*chairman].chairs.reset();
        news_.push_back("The shareholders of " + c.name() + " have voted " + who + " out as chairman");
        appoint_chairman(c.id(), chairman);
    }
}

void World::appoint_chairman(CompanyId cid, std::optional<PlayerId> excluded) {
    // The biggest shareholder who runs nothing else.
    std::optional<PlayerId> pick;
    std::int64_t most = 0;
    for (std::size_t i = 0; i < market_.investors.size(); ++i) {
        const Investor& inv = market_.investors[i];
        if (excluded == static_cast<PlayerId>(i) || inv.chairs) continue;
        if (inv.shares_in(cid) > most) {
            pick = static_cast<PlayerId>(i);
            most = inv.shares_in(cid);
        }
    }
    // Otherwise the board brings in a tycoon not yet in the game [I].
    if (!pick) {
        for (std::size_t t = 0; t < data_.tycoons.all().size() && !pick; ++t) {
            const bool playing = std::any_of(rivals_.begin(), rivals_.end(), [&](const Rival& r) { return r.tycoon == t; });
            if (playing) continue;
            Investor inv;
            inv.name = data_.tycoons.all()[t].name;
            inv.cash = Money::dollars(data_.balance.stock.starting_personal_cash);
            pick = static_cast<PlayerId>(market_.investors.size());
            market_.investors.push_back(std::move(inv));
            rivals_.push_back({*pick, t, -1'000'000, {}});
        }
    }
    if (!pick) {
        news_.push_back(company(cid).name() + " has no chairman");
        return;
    }
    market_.investors[*pick].chairs = cid;
    news_.push_back(market_.investors[*pick].name + " is now chairman of " + company(cid).name());
    note_player_company();
}

void World::run_rivals() {
    if (!rival_ai_) return;
    for (Rival& r : rivals_) run_rival(*this, r, data_.tycoons.all()[r.tycoon]);
}

void World::on_new_year() {
    for (Company& c : market_.companies) {
        if (!c.defunct()) c.close_year();
    }
    review_chairmen();
    const Economy::YearEnd sites = economy_.close_year(data_.industries, data_.balance.industries, rng_);
    for (const SiteId s : sites.closed) {
        news_.push_back("The " + data_.industries.get(economy_.sites()[s].type).name + " has closed after years of losses");
    }
    for (const SiteId s : sites.upgraded) {
        news_.push_back("The " + data_.industries.get(economy_.sites()[s].type).name + " has expanded to meet demand");
    }
    if (industries_appear_) {
        for (const SiteId id : spawn_industries(economy_, terrain_, data_.cargo, data_.industries, rng_, date_.year(),
                                                data_.balance)) {
            // Name the nearest town, for the news.
            const Site& s = economy_.sites()[id];
            std::string near;
            std::int32_t best = 1 << 20;
            for (const Town& t : economy_.towns()) {
                const std::int32_t d = std::max(std::abs(t.cx - s.cx), std::abs(t.cy - s.cy));
                if (d < best) {
                    best = d;
                    near = t.name;
                }
            }
            news_.push_back("A new " + data_.industries.get(s.type).name + " has opened" +
                            (near.empty() ? std::string() : " near " + near));
        }
    }
    for (Company& c : market_.companies) {
        c.retire_matured_bonds(date_.year());
        c.start_year(date_.year());
    }
}

} // namespace railmaster::sim
