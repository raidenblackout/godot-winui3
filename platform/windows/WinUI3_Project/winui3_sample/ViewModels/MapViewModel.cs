// MapViewModel.cs
// Provides indoor-map and rooms JSON payloads to GDScript when it issues
// `request_data` calls. Reads the bundled Assets/*.json files written next to
// the sample executable at build time.

namespace GodotWinUI3Sample.ViewModels;

using System;
using System.IO;
using System.Text.Encodings.Web;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Text.Json.Serialization;

public sealed class IndoorMap
{
	public string selectedFloor = string.Empty;
	// JSON-encoded string (NOT a nested object). The GDScript IndoorMap
	// parser expects mapData as a string and re-parses it — see the
	// Resources/TestResource/DataSet_1/result_get_indoor_map.json shape.
	public string mapData = string.Empty;
	public string mapCreationStatus = string.Empty;
	public string mapCreationType = string.Empty;
	public string floorLimit = string.Empty;
	public string selectedRoomId = string.Empty;
}

[JsonConverter(typeof(IMDFFloorPropertyConverter))]
public sealed class IMDFFloorProperty
{
	public string? color;
	public int scale;
	public string? generationMethod;
}

public sealed class IMDFFloorPropertyConverter : JsonConverter<IMDFFloorProperty>
{
	public override IMDFFloorProperty Read(ref Utf8JsonReader reader, Type typeToConvert, JsonSerializerOptions options)
	{
		var property = new IMDFFloorProperty();
		while (reader.Read())
		{
			if (reader.TokenType == JsonTokenType.EndObject) break;
			if (reader.TokenType != JsonTokenType.PropertyName) continue;

			var name = reader.GetString();
			reader.Read();
			switch (name)
			{
				case "color":
					property.color = reader.GetString() ?? string.Empty;
					break;
				case "scale":
					property.scale = reader.GetInt32();
					break;
				case "generationMethod":
					property.generationMethod = reader.GetString() ?? string.Empty;
					break;
				default:
					reader.Skip();
					break;
			}
		}
		return property;
	}

	public override void Write(Utf8JsonWriter writer, IMDFFloorProperty value, JsonSerializerOptions options)
	{
		writer.WriteStartObject();
		writer.WriteString("color", value.color);
		writer.WriteNumber("scale", value.scale);
		writer.WriteString("generationMethod", value.generationMethod);
		writer.WriteEndObject();
	}
}

internal static class IndoorMapUtility
{
	// Coerces any floor.properties.scale that arrived as a float into an int,
	// and inserts a default properties block when missing. Mirrors the parsing
	// behaviour of the GDScript side, which expects an integer scale.
	public static string CheckInvalidScale(string mapData)
	{
		var node = JsonNode.Parse(mapData);
		if (node is null) return mapData;

		var floors = node["floors"]?.AsArray();
		if (floors is null) return node.ToJsonString();

		foreach (var floor in floors)
		{
			if (floor is null) continue;

			if (floor["properties"] is null)
			{
				floor.AsObject().Add("properties", MakeNewPropertyNode());
				continue;
			}

			if (floor["properties"]?["scale"]?.AsValue().TryGetValue<float>(out var fValue) == true)
			{
				floor["properties"]!["scale"] = Convert.ToInt32(fValue);
			}
		}
		return node.ToJsonString();
	}

	private static JsonNode MakeNewPropertyNode()
	{
		var property = new IMDFFloorProperty { scale = 100 };
		var json = JsonSerializer.Serialize(property, new JsonSerializerOptions
		{
			IncludeFields = true,
			DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingDefault,
		});
		return JsonNode.Parse(json)!;
	}
}

public sealed class MapViewModel
{
	private readonly JsonSerializerOptions _options;
	private readonly string _assetsDir;

	public MapViewModel()
	{
		_options = new JsonSerializerOptions
		{
			IncludeFields = true,
			Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
			DefaultIgnoreCondition = JsonIgnoreCondition.WhenWritingNull,
		};
		_options.Converters.Add(new JsonStringEnumConverter(JsonNamingPolicy.CamelCase));

		_assetsDir = Path.Combine(AppContext.BaseDirectory, "Assets");
	}

	public string GetIndoorMap()
	{
		var indoorMap = new IndoorMap
		{
			mapCreationType = "ADDRESS_SEARCH",
			selectedFloor = "1",
			floorLimit = "3",
			mapCreationStatus = "none",
		};

		var path = Path.Combine(_assetsDir, "updatedMap.json");
		if (File.Exists(path))
		{
			var data = File.ReadAllText(path);
			data = IndoorMapUtility.CheckInvalidScale(data);
			indoorMap.mapData = data;
		}

		return JsonSerializer.Serialize(indoorMap, _options);
	}

	public string GetRooms() => ReadAssetJson("rooms.json", fallback: "[]");
	public string GetScenes() => ReadAssetJson("scenes.json", fallback: "[]");
	public string GetDevices() => ReadAssetJson("devices.json", fallback: "[]");
	public string GetLocations() => ReadAssetJson("locations.json", fallback: "[]");
	public string GetCapabilityStatus() => ReadAssetJson("capabilityStatus.json", fallback: "[]");

	// Reads an Assets/*.json file and re-serialises through JsonDocument so the
	// payload is normalised (no BOM, consistent whitespace) and validated as
	// well-formed JSON before being sent to the engine.
	private string ReadAssetJson(string fileName, string fallback)
	{
		var path = Path.Combine(_assetsDir, fileName);
		if (!File.Exists(path)) return fallback;
		using var doc = JsonDocument.Parse(File.ReadAllText(path));
		return JsonSerializer.Serialize(doc.RootElement, _options);
	}
}
