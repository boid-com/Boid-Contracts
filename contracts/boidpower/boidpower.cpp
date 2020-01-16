/**
 *  @file
 *  @copyright TODO
*/

#include "boidpower.hpp"
#include <math.h>
#include <inttypes.h>
#include <stdio.h>

// ------------------------ Action methods

void boidpower::regregistrar(name registrar, name tokencontract)
{
  require_auth(get_self());
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  
  if (cfg_i == cfg_t.end()) {
    cfg_t.emplace(get_self(), [&](auto& a) {
      a.id = 0;
      a.registrar = registrar;
      a.boidtoken_c = tokencontract;
      a.min_weight = 100;
      a.payout_multiplier = 0.01;
    });
  } else {
    cfg_t.modify(cfg_i, get_self(), [&](auto& a) {
      a.registrar = registrar;
      a.boidtoken_c = tokencontract;
    });
  }
}

void boidpower::regvalidator(name validator)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  accounts acct_t(cfg.boidtoken_c, validator.value);
  auto val_acct = acct_t.find(symbol("BOID",4).code().raw());
  check(val_acct != acct_t.end(), "Validator account must exist in boidtoken contract");

  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i == val_t.end(), "Validator already registered");
  val_t.emplace(cfg.registrar, [&](auto& a) {
    a.account = validator;
  });
}

void boidpower::addvalprot(name validator, uint64_t protocol_type, float weight)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;

  require_auth(cfg.registrar);
  
  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i != val_t.end(), "Validator not registered");
  
  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(protocol_type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist");  
  
  val_t.modify(val_i, cfg.registrar, [&](auto& a) {
    a.weights[protocol_type] = weight;
  });
}

void boidpower::delvalidator(name validator)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i != val_t.end(),
    "Validator does not exist under registrar");
  
  val_t.erase(val_i);
}

void boidpower::updaterating(
  name validator,
  string device_name,
  uint64_t round_start,
  uint64_t round_end,
  float rating,
  uint64_t units,
  uint64_t type
)
{
  bool reset_validators = false, add_post_reset = false;
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  const auto& cfg = *cfg_i;
  
  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i != val_t.end(), "Account not registered as validator");
  const auto& val = *val_i;

  require_auth(validator);
  
  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist");  

  check(device_exists(device_name), "Device does not exist");
  bool exists;
  uint64_t device_key;
  get_device_key(device_name, &exists, &device_key);
  uint64_t protocol_type = get_protocol_type(device_name);
  device_t dev_t(get_self(), protocol_type); 
  auto dev_i = dev_t.find(device_key);

  accounts acct_t(cfg.boidtoken_c, dev_i->owner.value);
  auto dev_owner = acct_t.find(symbol("BOID",4).code().raw());
  check(dev_owner != acct_t.end(), "Account must exist in boidtoken contract");

  power_t p_t(get_self(), device_key);
  auto p_i = p_t.find(type);
  const auto& rtg = *p_i;

  check(valid_round(round_start,round_end),
    "Round times invalid");
  
  check(rating > 0,
    "Rating must be greater than 0");

  uint64_t closest_round_start = get_closest_round(round_start);
  uint64_t closest_round_end = get_closest_round(round_end);
  microseconds closest_round_start_us = microseconds(closest_round_start);
  microseconds closest_round_end_us = microseconds(closest_round_end);

  if (p_i != p_t.end()) {
    check(
      closest_round_start >= rtg.round_start.count(),
      "Validator attempting to validate for a prior round"
    );
  }

  if (p_i == p_t.end()) {
    p_t.emplace(validator, [&](auto& a) {
      a.type = type;
      a.ratings[validator.value] = rating;
      a.units[validator.value] = units;
      a.round_start = closest_round_start_us;
      a.round_end = closest_round_end_us;
    });
  } else {
    if (rtg.round_start.count() < round_start) {
      reset_validators = true;
      add_post_reset = true;
      //TODO check if overwriting unvalidated data
    } else {
      p_t.modify(p_i, validator, [&](auto& a) {
        a.type = type;
        a.ratings[validator.value] = rating;
        a.units[validator.value] = units;
        a.round_start = closest_round_start_us;
        a.round_end = closest_round_end_us;
      });
    }
  }

  if (get_weight(device_key, type) >= cfg.min_weight) {
    uint64_t median_units;
    float median_value = get_median_rating(device_key, type, &median_units);
    action(
      permission_level{cfg.boidtoken_c,"poweroracle"_n},
      cfg.boidtoken_c,
      "updatepower"_n,
      std::make_tuple(dev_i->owner, median_value)
    ).send();
    dev_t.modify(dev_i, same_payer, [&](auto& a) {
      a.units += median_units;
    });
    reset_validators = true;
  }

  if (reset_validators) {
  reset_ratings(device_key, type);
  }
  
  if (add_post_reset) {
    print(
      "round start: ", closest_round_start,
      "\nround end: ", closest_round_end
    );
    p_t.modify(p_i, validator, [&](auto& a) {
      a.type = type;
      a.ratings[validator.value] = rating;
      a.round_start = closest_round_start_us;
      a.round_end = closest_round_end_us;
    });
  }
}

void boidpower::addprotocol(string protocol_name, string description, float difficulty, string meta)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  check(description.size() <= 256, "description has more than 256 bytes");

  protocol_t protoc_t(get_self(), cfg.registrar.value);
  for (auto it = protoc_t.begin(); it != protoc_t.end(); it++) {
    check(
      protocol_name != it->protocol_name,
      "Protocol already exists with this name. Refer to existing protocol description to see if they may be the same."
    );
  }
  
  protoc_t.emplace(cfg.registrar, [&](auto& a) {
    //could also do sha256 on protocol_name    
    a.type = protoc_t.available_primary_key();
    a.protocol_name = protocol_name;
    a.description = description;
    a.meta = meta;
    a.difficulty = difficulty;
  });
}

void boidpower::newprotdiff(uint64_t protocol_type, float difficulty)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(protocol_type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist");
  
  protoc_t.modify(protoc_i, same_payer, [&](auto& a) {
    a.difficulty = difficulty;
  });
}

void boidpower::newprotmeta(uint64_t protocol_type, string meta)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(protocol_type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist");
  
  protoc_t.modify(protoc_i, same_payer, [&](auto& a) {
    a.meta = meta;
  });
}

void boidpower::regdevice(name owner, string device_name, uint64_t protocol_type, bool registrar_registration)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  const auto& cfg = *cfg_i;

  name payer;
  if (registrar_registration) {
    require_auth(cfg.registrar);
    payer = cfg.registrar;
  } else {
    require_auth(owner);
    payer = owner;
  }

  accounts boidaccounts(cfg.boidtoken_c, owner.value);
  auto owner_acct = boidaccounts.find(symbol("BOID",4).code().raw());
  check(owner_acct != boidaccounts.end(), "Owner account must exist in boidtoken contract");

  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(protocol_type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist"); 

  device_t dev_t(get_self(), protocol_type);
  string prefixed_name = std::to_string(protocol_type) + "_" + device_name;//protoc_i->meta["prefix"] + device_name;
  checksum256 device_hash = sha256(prefixed_name.c_str(),prefixed_name.length());
  auto arr = device_hash.extract_as_byte_array().data();
  uint64_t device_key = hash2key(device_hash);
  auto dev_i = dev_t.find(device_key);
  bool valid_name = true;
  uint64_t collision_modifier = 0;
  while (dev_i != dev_t.end()) {
    if (dev_i->device_name.compare(prefixed_name) == 0) {
      valid_name = false;
      break;
    }
    collision_modifier++;
    device_key++;
    dev_i = dev_t.find(device_key);
  }
  check(valid_name, "Device already registered");

  dev_t.emplace(payer, [&](auto& a) {
    a.device_key = device_key;
    a.device_name = prefixed_name;
    a.owner = owner;
    a.collision_modifier = collision_modifier;
    a.units = 0;
  });
}

void boidpower::regpayacct(name payout_account)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);

  accounts acct_t(cfg.boidtoken_c, payout_account.value);
  auto val_acct = acct_t.find(symbol("BOID",4).code().raw());
  check(val_acct != acct_t.end(), "Payout account must exist in boidtoken contract");

  cfg_t.modify(cfg_i, get_self(), [&](auto& a) {
    a.payout_account = payout_account;
  });
}

void boidpower::payout(name validator, bool registrar_payout)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;

  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i != val_t.end(), "Account not registered as validator");

  check(registrar_payout, "Only registrar can issue payouts at this time");
  if (registrar_payout) require_auth(cfg.registrar);
  else require_auth(validator);

  symbol sym = symbol("BOID",4);
  float precision_coef = pow(10,4);
  int64_t payout_amount = 
    (int64_t)precision_coef*cfg.payout_multiplier*val_i->num_unpaid_validations;
  asset payout_quantity = asset{payout_amount,sym};

  string memo =
    "Payout of " + payout_quantity.to_string() +\
    " tokens to validator " +\
    validator.to_string();
  action(
    permission_level{cfg.payout_account,"poweroracle"_n},
    cfg.boidtoken_c,
    "transfer"_n,
    std::make_tuple(cfg.payout_account, validator, payout_quantity, memo)
  ).send();

  val_t.modify(val_i, same_payer, [&](auto& a){
    a.num_unpaid_validations = 0;
    if (a.total_payout.symbol.code().raw() != sym.code().raw())
      a.total_payout = payout_quantity;
    else
      a.total_payout += payout_quantity;
  });
}

void boidpower::setminweight(float min_weight)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  cfg_t.modify(cfg_i, cfg.registrar, [&](auto& a) {
    a.min_weight = min_weight;
  });
}

void boidpower::setpayoutmul(float payout_multiplier)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  cfg_t.modify(cfg_i, cfg.registrar, [&](auto& a) {
    a.payout_multiplier = payout_multiplier;
  });
}

void boidpower::delprotocol(uint64_t protocol_type){
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  protocol_t protoc_t(get_self(), cfg.registrar.value);
  auto protoc_i = protoc_t.find(protocol_type);
  check(protoc_i != protoc_t.end(), "Protocol does not exist");
  
  protoc_t.erase(protoc_i);
}

void boidpower::deldevice(uint64_t protocol_type, string device_name)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  require_auth(cfg.registrar);
  
  device_t dev_t(get_self(), protocol_type);
  checksum256 device_hash = sha256(device_name.c_str(),device_name.length());
  auto arr = device_hash.extract_as_byte_array().data();
  uint64_t device_key = hash2key(device_hash);
  auto dev_i = dev_t.find(device_key);

  bool device_exists = false;
  while (dev_i != dev_t.end()) {
    if (dev_i->device_name.compare(device_name) == 0) {
      device_exists = true;
      break;
    }
    device_key++;
    dev_i = dev_t.find(device_key);
  }

  check(device_exists, "Device does not exist");
  
  dev_t.erase(dev_i);
}

void boidpower::delrating(name validator, string device_name)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Must first add configuration");
  const auto& cfg = *cfg_i;
  
  validator_t val_t(get_self(), cfg.registrar.value);
  auto val_i = val_t.find(validator.value);
  check(val_i != val_t.end(), "Account not registered as validator");

  require_auth(cfg.registrar);

  check(device_exists(device_name), "Device does not exist");
  bool exists;
  uint64_t device_key;
  get_device_key(device_name, &exists, &device_key);
  uint64_t protocol_type = get_protocol_type(device_name);

  power_t p_t(get_self(), device_key);
  auto p_i = p_t.find(protocol_type);
  
  check(p_i != p_t.end(), "Power rating for device:protocol does not exist");
  
  p_t.erase(p_i);
}

void boidpower::delconfig()
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  check(cfg_i != cfg_t.end(),"Configuration does not exist");
  require_auth(cfg_i->registrar);

  cfg_t.erase(cfg_i);
}

// ------------------------ Non-action methods
void boidpower::reset_ratings(uint64_t device_key, uint64_t type)
{
  power_t p_t(get_self(), device_key);
  auto p_i = p_t.find(type);
  
  if (p_i != p_t.end()) {
    p_t.modify(p_i, same_payer, [&](auto& a) {
      a.ratings.clear();
      a.units.clear();
      // March forward one round
      a.round_start = microseconds(get_closest_round(a.round_end.count()));
      a.round_end = microseconds(get_closest_round(a.round_end.count() + ROUND_LENGTH));
    });
  }
}

bool boidpower::same_round(
  uint64_t round_start, uint64_t round_end,
  uint64_t real_round_start, uint64_t real_round_end
)
{
  return round_start == real_round_start &&
         round_end == real_round_end;
}

bool boidpower::valid_round(uint64_t round_start, uint64_t round_end)
{
  print("start: ", round_start);
  print("closest round start: ", get_closest_round(round_start));
  print("end: ", round_end);
  print("closest round end: ", get_closest_round(round_end));
  return true;
  return  llabs(round_start - get_closest_round(round_start)) < 60e6 &&
          llabs(round_end - get_closest_round(round_end)) < 60e6 &&
          get_closest_round(round_end) - get_closest_round(round_start) == ROUND_LENGTH;
}

float boidpower::get_weight(uint64_t device_key, uint64_t type)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  const auto& cfg = *cfg_i;
  
  validator_t val_t(get_self(), cfg.registrar.value);
  
  power_t p_t(get_self(), device_key);
  auto p_i = p_t.find(type);
  
  float weight = 0;
  for (auto it = p_i->ratings.begin(); it != p_i->ratings.end(); it++) {
    uint64_t validator_value = it->first;
    auto val_i = val_t.find(validator_value);
    weight += val_i->weights.at(type);
  }
  
  return weight;
}

float boidpower::get_median_rating(uint64_t device_key, uint64_t type, uint64_t* units)
{
  config_t cfg_t(get_self(), get_self().value);
  auto cfg_i = cfg_t.find(0);
  const auto& cfg = *cfg_i;
  
  validator_t val_t(get_self(), cfg.registrar.value);
  
  power_t p_t(get_self(), device_key);
  auto p_i = p_t.find(type);
  
  vector<float> power_ratings;
  for (auto it = p_i->ratings.begin(); it != p_i->ratings.end(); it++) {
    power_ratings.push_back(it->second);
  }
  sort(power_ratings.begin(), power_ratings.end());
  float med = quant(power_ratings, 0.50);
  float q1 = quant(power_ratings, 0.25);
  float q3 = quant(power_ratings, 0.75);
  float iqr = q3 - q1;
  float outlier_low = q1 - iqr;
  float outlier_high = q3 + iqr;
  print("median: ", med, "\n");
  print("q1: ", q1, "\n");
  print("q3: ", q3, "\n");
  print("iqr: ", iqr, "\n");
  print("outlier low: ", outlier_low, "\n");
  print("outlier high: ", outlier_high, "\n");
  
  float eps = 0.001;
  for (auto it = p_i->ratings.begin(); it != p_i->ratings.end(); it++) {
    if (fabs(it->second - med) < eps) {
      *units = p_i->units.at(it->first);
    }
    if (it->second > outlier_high || it->second < outlier_low) {
      auto val_i = val_t.find(it->first);
      val_t.modify(val_i, same_payer, [&](auto& a){
        a.num_outliers++;
      });
    } else {
      auto val_i = val_t.find(it->first);
      val_t.modify(val_i, same_payer, [&](auto& a){
        a.num_validations++;
        a.num_unpaid_validations++;
      });      
    }
  }
  
  return med;
}

bool boidpower::device_exists(string device_name)
{
  device_t dev_t(get_self(), get_protocol_type(device_name));
  checksum256 device_hash = sha256(device_name.c_str(),device_name.length());
  auto arr = device_hash.extract_as_byte_array().data();
  uint64_t device_key = hash2key(device_hash);
  auto dev_i = dev_t.find(device_key);

  bool device_exists = false;
  while (dev_i != dev_t.end()) {
    if (dev_i->device_name.compare(device_name) == 0) {
      device_exists = true;
      break;
    }
    device_key++;
    dev_i = dev_t.find(device_key);
  }
  return device_exists;
}

void boidpower::get_device_key(string device_name, bool* exists, uint64_t* device_key)
{
  device_t dev_t(get_self(), get_protocol_type(device_name));
  checksum256 device_hash = sha256(device_name.c_str(),device_name.length());
  auto arr = device_hash.extract_as_byte_array().data();
  *device_key = hash2key(device_hash);
  auto dev_i = dev_t.find(*device_key);

  bool dev_exists = false;
  while (dev_i != dev_t.end()) {
    if (dev_i->device_name.compare(device_name) == 0) {
      dev_exists = true;
      break;
    }
    (*device_key)++;
    dev_i = dev_t.find(*device_key);
  }
  *exists = dev_exists;
}

uint64_t boidpower::get_protocol_type(string device_name)
{
  size_t prefix_end = device_name.find_first_of('_');
  return stoi(device_name.substr(0,prefix_end));
}

uint64_t boidpower::get_closest_round(uint64_t t)
{
  return ROUND_LENGTH*((uint64_t)round((float)t/(float)ROUND_LENGTH));
}

uint64_t boidpower::hash2key(checksum256 hash)
{
  auto arr = hash.extract_as_byte_array();
  uint64_t key = 0;
  key = (uint64_t)arr[7] << (56) |\
        (uint64_t)arr[6] << (48) |\
        (uint64_t)arr[5] << (40) |\
        (uint64_t)arr[4] << (32) |\
        (uint64_t)arr[3] << (24) |\
        (uint64_t)arr[2] << (16) |\
        (uint64_t)arr[1] << (8) |\
        (uint64_t)arr[0] << (0);
  return key;
}