
# Foxtrot Serialize

> [!WARNING]
> This library is designed for personal use, so there may be unforeseen bugs and issues!


A small serialization library for C++, created for [foxtrot](https://github.com/emd22/foxtrot).

This is C++20 only due to the usage of concepts, but could be easily modified for older versions of C++.


## Usage
To mark values in a struct as serializable, use the `FX_SERIALIZABLE_MEMBERS` macro.
```cpp
struct ExampleStruct
{
    int SomeValue;
    float32 SomeFloat;
    std::string SomeString = "Hello, World";

    FX_SERIALIZABLE_MEMBERS(SomeValue, SomeFloat, SomeString);
};
```

Then, to serialize an instance of an object:

```cpp
FxSerializerIO writer;

ExampleStruct example { 10, 20.0f };
example.WriteTo(FxHashStr("NameOfObject"), writer);
```

Which can later be read using a similar method:

```cpp
FxSerializerIO reader;

ExampleStruct read_data; // Ensure that ExampleStruct
read_data.ReadFrom(FxHashStr("NameOfObject"), reader);
```

As well, you can serialize nested objects:
```cpp
struct Vec3f
{
    int32 X, Y, Z;
    FX_SERIALIZABLE_MEMBERS(X, Y, Z);
};

struct Player
{
    std::string Name;
    Vec3f Position;
    Vec3f Rotation;

    FX_SERIALIZABLE_MEMBERS(Name, Position, Rotation);
};

void SaveData()
{
    FxSerializerIO writer;
    
    Player player{};
    // ... Set player values ...
    player.WriteTo(FxHashStr("MainPlayer"), writer);

    writer.SaveToFile("PlayerSave.fxsd");
}
```


### File Input/Output

The current state in FxSerializerIO can be written and read from a file to the types and data.

Writing to a file:
```cpp
FxSerializerIO writer;
writer.WriteToFile("MyFavoriteStruct.fxsd");
```

And similarly for reading:

```cpp
FxSerializerIO reader;
reader.ReadFromFile("MyFavoriteStruct.fxsd");
```

## Building the Example

```sh
cc -std=c++20 FxSerializer.cpp Example.cpp
./a.out
```
