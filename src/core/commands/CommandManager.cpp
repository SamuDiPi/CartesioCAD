#include "core/commands/CommandManager.h"

#include <utility>

#include <cctype>

namespace cartesio {

namespace {

std::string toUpper(std::string s)
{
    for (char& c : s)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

void CommandManager::add(const std::string& name, std::initializer_list<std::string> aliases, Factory factory)
{
    const std::string key = toUpper(name);
    factories_[key] = std::move(factory);
    aliases_[key] = key;
    for (const auto& alias : aliases)
        aliases_[toUpper(alias)] = key;
}

bool CommandManager::execute(const std::string& nameOrAlias, CommandContext& ctx)
{
    const auto it = aliases_.find(toUpper(nameOrAlias));
    if (it == aliases_.end())
        return false;

    if (active_)
        cancel(ctx);

    active_ = factories_.at(it->second)();
    activeName_ = it->second;
    last_ = it->second;
    handle(active_->start(ctx));
    return true;
}

void CommandManager::start(std::unique_ptr<Command> command, const std::string& name, CommandContext& ctx)
{
    if (active_)
        cancel(ctx);
    active_ = std::move(command);
    activeName_ = name;
    handle(active_->start(ctx));
}

std::string CommandManager::prompt() const
{
    return active_ ? active_->prompt() : std::string{};
}

std::vector<std::string> CommandManager::commandNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, factory] : factories_)
        names.push_back(name);
    return names;
}

std::vector<std::string> CommandManager::commandWords() const
{
    std::vector<std::string> words;
    for (const auto& [alias, name] : aliases_)
        words.push_back(alias);
    return words;
}

void CommandManager::point(CommandContext& ctx, Vec2 p)
{
    if (active_)
        handle(active_->onPoint(ctx, p));
}

void CommandManager::value(CommandContext& ctx, double v)
{
    if (active_)
        handle(active_->onValue(ctx, v));
}

void CommandManager::keyword(CommandContext& ctx, const std::string& word)
{
    if (active_)
        handle(active_->onKeyword(ctx, toUpper(word)));
}

void CommandManager::text(CommandContext& ctx, const std::string& text)
{
    if (active_)
        handle(active_->onText(ctx, text));
}

void CommandManager::enter(CommandContext& ctx)
{
    if (active_)
        handle(active_->onEnter(ctx));
}

void CommandManager::cancel(CommandContext& ctx)
{
    if (!active_)
        return;
    active_->cancel(ctx);
    active_.reset();
    const std::string name = std::exchange(activeName_, std::string());
    if (finished)
        finished(name, true);
}

void CommandManager::handle(CommandStatus status)
{
    if (status == CommandStatus::Finished) {
        active_.reset();
        const std::string name = std::exchange(activeName_, std::string());
        if (finished)
            finished(name, false);
    }
}

} // namespace cartesio
