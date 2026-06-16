#include "propertyBridge.h"

#include "gds_property_codec.h"

#include "dbPropertiesRepository.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace coredb
{

namespace {

tl::Variant decode_gds_typed_value (const std::string &value)
{
  if (value.compare (0, 3, "i2:") == 0) {
    return tl::Variant (std::stol (value.substr (3)));
  }
  if (value.compare (0, 3, "i4:") == 0) {
    return tl::Variant (std::stol (value.substr (3)));
  }
  if (value.compare (0, 2, "s:") == 0) {
    return tl::Variant (value.substr (2));
  }
  if (value.compare (0, 5, "rhex:") == 0) {
    return tl::Variant (value);
  }
  return tl::Variant (value);
}

std::string encode_gds_typed_value (const tl::Variant &value)
{
  if (value.is_nil ()) {
    return std::string ();
  }

  if (value.is_a_string ()) {
    const std::string &text = value.to_string ();
    if (text.compare (0, 3, "i2:") == 0 ||
        text.compare (0, 3, "i4:") == 0 ||
        text.compare (0, 5, "rhex:") == 0 ||
        text.compare (0, 2, "s:") == 0) {
      return text;
    }
    return std::string ("s:") + text;
  }

  if (value.can_convert_to_long ()) {
    const long number = value.to_long ();
    if (number >= std::numeric_limits<std::int16_t>::min () &&
        number <= std::numeric_limits<std::int16_t>::max ()) {
      return std::string ("i2:") + std::to_string (number);
    }
    return std::string ("i4:") + std::to_string (number);
  }

  if (value.can_convert_to_double ()) {
    return std::string ("s:") + value.to_string ();
  }

  return std::string ("s:") + value.to_string ();
}

} // namespace

db::properties_id_type properties_id_from_core (const std::vector<core::Property> &properties)
{
  if (properties.empty ()) {
    return 0;
  }

  db::PropertiesSet property_set;
  for (const core::Property &property : properties) {
    if (core::gds_prop::isGdsProperty (property)) {
      const std::optional<std::int16_t> attr = core::gds_prop::attrOf (property);
      if (! attr) {
        continue;
      }
      property_set.insert (tl::Variant (long (*attr)), decode_gds_typed_value (property.value));
    } else if (! property.name.empty ()) {
      property_set.insert (tl::Variant (property.name), tl::Variant (property.value));
    }
  }

  if (property_set.empty ()) {
    return 0;
  }
  return db::properties_id (property_set);
}

std::vector<core::Property> properties_from_klayout (db::properties_id_type prop_id)
{
  std::vector<core::Property> properties;
  if (prop_id == 0) {
    return properties;
  }

  const std::multimap<tl::Variant, tl::Variant> property_map = db::properties (prop_id).to_map ();
  properties.reserve (property_map.size ());

  for (auto it = property_map.begin (); it != property_map.end (); ++it) {
    const tl::Variant &name = it->first;
    const tl::Variant &value = it->second;

    long attr = -1;
    if (name.can_convert_to_long ()) {
      attr = name.to_long ();
    }

    if (attr >= 0 && attr <= std::numeric_limits<std::uint16_t>::max ()) {
      properties.push_back (core::gds_prop::make (
        static_cast<std::int16_t> (attr),
        encode_gds_typed_value (value)));
    } else if (! name.is_nil ()) {
      properties.push_back (core::Property (name.to_string (), value.to_string ()));
    }
  }

  return properties;
}

} // namespace coredb
