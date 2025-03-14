#include "PrecompiledHeader.h"

#include "Damage.h"
#include "MTGCardInstance.h"
#include "Counters.h"
#include "WEvent.h"
#include "Translate.h"
#include "WResourceManager.h"
#include "GameObserver.h"
#include "WParsedInt.h"

Damage::Damage(GameObserver* observer, MTGCardInstance * source, Damageable * target)
    : Interruptible(observer)
{
    init(source, target, source->getPower(), DAMAGE_OTHER);
}

Damage::Damage(GameObserver* observer, MTGCardInstance * source, Damageable * target, int damage, DamageType _typeOfDamage)
    : Interruptible(observer)
{
    init(source, target, damage, _typeOfDamage);
}

void Damage::init(MTGCardInstance * _source, Damageable * _target, int _damage, DamageType _typeOfDamage)
{
    typeOfDamage = _typeOfDamage;
    target = _target;
    if(_source  && _source->name.empty() && _source->storedSourceCard) // Fix for damage dealt inside ability$!!$ keyword.
        source = _source->storedSourceCard;
    else
        source = _source;

    if (_damage < 0)
        _damage = 0; //Negative damages cannot happen
    damage = _damage;
    mHeight = 40;
    type = ACTION_DAMAGE;
}

int Damage::resolve()
{
    if (damage < 0)
        damage = 0; //Negative damages cannot happen
    state = RESOLVED_OK;
    WEvent * e = NEW WEventDamage(this);
    //Replacement Effects
    e = observer->replacementEffects->replace(e);
    if (!e)
        return 0;
    WEventDamage * ev = dynamic_cast<WEventDamage*> (e);
    if (!ev)
    {
        observer->receiveEvent(e);
        return 0;
    }
    damage = ev->damage->damage;
    target = ev->damage->target;
    if (!damage)
    {
        delete (e);
        return 0;
    }

    //asorbing effects for cards controller-----------

    //reserved for culmulitive absorb ability coding

    //prevent next damage-----------------------------
    if (target->preventable > 0)
    {
        int preventing = MIN(target->preventable, damage);
        damage -= preventing;
        target->preventable -= preventing;
    }
    else
    {
        target->preventable = 0;
    }

    //-------------------------------------------------
    //Ajani Steadfast ---
    if(target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE && (((MTGCardInstance*)target)->hasType("planeswalker") || ((MTGCardInstance*)target)->hasType("battle")) && ((MTGCardInstance*)target)->controller()->forcefield)
        damage = 1;
    if (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE)
    {
        MTGCardInstance * _target = (MTGCardInstance *) target;
        if ((_target)->protectedAgainst(source))
            damage = 0;
        //rulings = 10/4/2004    The damage prevention ability works even if it has no counters, as long as some effect keeps its toughness above zero.
        //these creature are essentially immune to damage. however 0/-1 effects applied through lords or counters can kill them.
        if ((_target)->has(Constants::PROTECTIONFROMCOLOREDSPELLS))
        {//damage is prevented as long as the damage source is a spell on the stack...
            if((source->currentZone == source->controller()->opponent()->game->stack||source->currentZone == source->controller()->game->stack) && (source->hasColor(1)||source->hasColor(2)||source->hasColor(3)||source->hasColor(4)||source->hasColor(5)))
                damage = 0;
        }
        if ((_target)->has(Constants::PHANTOM))
        {
            damage = 0;
            (_target)->counters->removeCounter(1, 1);
        }
        if ((_target)->has(Constants::ABSORB))
        {
            damage -=  (_target)->basicAbilities[(int)Constants::ABSORB];
            if(damage < 0)
                damage = 0;
        }
        if ((_target)->has(Constants::WILTING))
        {
            for (int j = damage; j > 0; j--)
            {
                (_target)->counters->addCounter(-1, -1);
            }
            damage = 0;
        }
        if ((_target)->has(Constants::VIGOR))
        {
            for (int j = damage; j > 0; j--)
            {
                (_target)->counters->addCounter(1, 1);
            }
            damage = 0;
        }
        if ((_target)->has(Constants::NONCOMBATVIGOR) && typeOfDamage != DAMAGE_COMBAT)
        {
            for (int j = damage; j > 0; j--)
            {
                (_target)->counters->addCounter(1, 1);
            }
            damage = 0;
        }
        if ((_target)->has(Constants::HYDRA))
        {
            for (int j = damage; j > 0; j--)
            {
                (_target)->counters->removeCounter(1, 1);
            }
            damage = 0;
        }
        if (!damage)
        {
            state = RESOLVED_NOK;
            delete (e);
            return 0;
        }
        _target->doDamageTest = 1;
    }
    if (target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER)
    {//Ajani Steadfast
        if(((Player*)target)->forcefield)
            damage = 1;
        if(source->has(Constants::LIBRARYEATER) && typeOfDamage == 1)
        {
            for (int j = damage; j > 0; j--)
            {
                if(((Player*)target)->game->library->nb_cards)
                    ((Player*)target)->game->putInZone(((Player*)target)->game->library->cards[((Player*)target)->game->library->nb_cards - 1], ((Player*)target)->game->library, ((Player*)target)->game->graveyard);
            }
            damage = 0;
        }
        if(source->alias == 89092 && typeOfDamage == 1)//Szadek Lord of Secrets
        {
            for (int j = damage; j > 0; j--)
            {
                if(((Player*)target)->game->library->nb_cards)
                    ((Player*)target)->game->putInZone(((Player*)target)->game->library->cards[((Player*)target)->game->library->nb_cards - 1], ((Player*)target)->game->library, ((Player*)target)->game->graveyard);

                source->counters->addCounter(1, 1);
            }
            damage = 0;
        }
        if (!damage)
        {
            state = RESOLVED_NOK;
            delete (e);
            return 0;
        }
    }
    int a = damage;

    if (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE && (source->has(Constants::WITHER) || source->has(
                    Constants::INFECT)))
    {
        // Damage for WITHER or poison on creatures. This should probably go in replacement effects
        MTGCardInstance * _target = (MTGCardInstance *) target;
        for (int i = 0; i < damage; i++)
        {
            _target->counters->addCounter(-1, -1);
        }
        if(_target->toughness <= 0 && _target->has(Constants::INDESTRUCTIBLE))
            _target->toGrave(true); // The indestructible creatures can have different destination zone after death.
    }
    else if (target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER && (source->has(Constants::INFECT)||source->has(Constants::POISONDAMAGER)))
    {
        // Poison on player
        Player * _target = (Player *) target;
        if(!_target->inPlay()->hasAbility(Constants::POISONSHROUD)){
            _target->poisonCount += damage;//this will be changed to poison counters.
            if(damage > 0)
            {
                WEvent * e = NEW WEventplayerPoisoned(_target, damage); // Added an event when player receives any poison counter.
                _target->getObserver()->receiveEvent(e);
            }
        }
        _target->damageCount += damage;
        if(typeOfDamage == 2)
            _target->nonCombatDamage += damage;
        if ( typeOfDamage == 1 && target == source->controller()->opponent() )//add vector prowledtypes.
        {
            vector<string> values = MTGAllCards::getCreatureValuesById();
            for (size_t i = 0; i < values.size(); ++i)
            {
                if ( source->hasSubtype( values[i] ) && find(source->controller()->prowledTypes.begin(), source->controller()->prowledTypes.end(), values[i])==source->controller()->prowledTypes.end() )
                    source->controller()->prowledTypes.push_back(values[i]);
            }
        }
    }
    else if ((target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER) && (source->getToxicity() > 0))
    {
        //Damage + poison counters on player
        Player * _target = (Player *) target;
        if(!_target->inPlay()->hasAbility(Constants::CANTCHANGELIFE))
            a = target->dealDamage(damage);
        target->damageCount += damage;
        if(typeOfDamage == 2)
            target->nonCombatDamage += damage;
        if ( typeOfDamage == 1 && target == source->controller()->opponent() )//add vector prowledtypes.
        {
            vector<string> values = MTGAllCards::getCreatureValuesById();
            for (size_t i = 0; i < values.size(); ++i)
            {
                if ( source->hasSubtype( values[i] ) && find(source->controller()->prowledTypes.begin(), source->controller()->prowledTypes.end(), values[i])==source->controller()->prowledTypes.end() )
                    source->controller()->prowledTypes.push_back(values[i]);
            }
        }
        if(!_target->inPlay()->hasAbility(Constants::POISONSHROUD))
        {
            _target->poisonCount += source->getToxicity();
             WEvent * e = NEW WEventplayerPoisoned(_target, damage); // Added an event when player receives any poison counter.
            _target->getObserver()->receiveEvent(e);
        }
    }
    else
    {
        // "Normal" case,
        //return the left over amount after effects have been applied to them.
        if ((target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER && ((Player *)target)->inPlay()->hasAbility(Constants::CANTCHANGELIFE)) ||
            (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE && (((MTGCardInstance*)target)->basicAbilities[Constants::UNDAMAGEABLE] == 1)))
            ;//do nothing
        else
            a = target->dealDamage(damage);
        if (!(target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE && (((MTGCardInstance*)target)->basicAbilities[Constants::UNDAMAGEABLE] == 1))){
            target->damageCount += damage;//the amount must be the actual damage so i changed this from 1 to damage, this fixes pdcount and odcount
            if(typeOfDamage == 2)
                target->nonCombatDamage += damage;
        }
        if (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE && (((MTGCardInstance*)target)->basicAbilities[Constants::UNDAMAGEABLE] == 0)){
            ((MTGCardInstance*)target)->wasDealtDamage += damage;
            ((MTGCardInstance*)source)->damageToCreature += damage;
        }
        if (target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER)
        {
            if(target == source->controller())
            {
                ((MTGCardInstance*)source)->damageToController += damage;
            }
            else
            {
                ((MTGCardInstance*)source)->damageToOpponent += damage;
                if(((MTGCardInstance*)source)->isCommander > 0 && typeOfDamage == DAMAGE_COMBAT)
                    ((MTGCardInstance*)source)->damageInflictedAsCommander += damage;
            }
            target->lifeLostThisTurn += damage;
            if ( typeOfDamage == 1 && target == source->controller()->opponent() )//add vector prowledtypes.
            {
                source->controller()->dealsdamagebycombat = 1; // for restriction check
                ((MTGCardInstance*)source)->combatdamageToOpponent += damage; //check
                vector<string> values = MTGAllCards::getCreatureValuesById();//getting a weird crash here. rarely.
                for (size_t i = 0; i < values.size(); ++i)
                {
                    if ( source->hasSubtype( values[i] ) && find(source->controller()->prowledTypes.begin(), source->controller()->prowledTypes.end(), values[i])==source->controller()->prowledTypes.end() )
                        source->controller()->prowledTypes.push_back(values[i]);
                }
            }
            WEvent * lifed = NEW WEventLife((Player*)target,-damage, source);
            observer->receiveEvent(lifed);
            if(((MTGCardInstance*)source)->damageInflictedAsCommander > 20) // If a player has been dealt 21 points of combat damage by a particular Commander during the game, that player loses a game.
                observer->setLoser(((MTGCardInstance*)source)->controller()->opponent());
        }
    }
    //Send (Damage/Replaced effect) event to listeners
    observer->receiveEvent(e);
    return a;
}
void Damage::Render()
{
    WFont * mFont = WResourceManager::Instance()->GetWFont(Fonts::MAIN_FONT);
    mFont->SetBase(0);
    mFont->SetScale(DEFAULT_MAIN_FONT_SCALE);
    char buffer[200];
    sprintf(buffer, _("Deals %i damage to").c_str(), damage);
    //mFont->DrawString(buffer, x + 20, y, JGETEXT_LEFT);
    mFont->DrawString(buffer, x + 32, y + GetVerticalTextOffset(), JGETEXT_LEFT);
    JRenderer * renderer = JRenderer::GetInstance();
    JQuadPtr quad = WResourceManager::Instance()->RetrieveCard(source, CACHE_THUMB);
    if (quad.get())
    {
        //float scale = 30 / quad->mHeight;
        //renderer->RenderQuad(quad.get(), x, y, 0, scale, scale);
        quad->SetColor(ARGB(255,255,255,255));
        float scale = mHeight / quad->mHeight;
        renderer->RenderQuad(quad.get(), x + (quad->mWidth * scale / 2), y + (quad->mHeight * scale / 2), 0, scale, scale);
    }
    else
    {
        //mFont->DrawString(_(source->getName()).c_str(), x, y - 15);
        mFont->DrawString(_(source->getName()).c_str(), x, y + GetVerticalTextOffset() - 15);
    }
    quad = target->getIcon();
    if (quad.get())
    {
        //float scale = 30 / quad->mHeight;
        //renderer->RenderQuad(quad.get(), x + 150, y, 0, scale, scale);
        float backupX = quad->mHotSpotX;
        float backupY = quad->mHotSpotY;
        quad->SetColor(ARGB(255,255,255,255));
        quad->SetHotSpot(quad->mWidth / 2, quad->mHeight / 2);
        float scale = mHeight / quad->mHeight;
        renderer->RenderQuad(quad.get(), x + 130, y - 0.5f + ((mHeight - quad->mHeight) / 2) + quad->mHotSpotY, 0, scale, scale);
        quad->SetHotSpot(backupX, backupY);
    }
    else
    {
        //if (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE)
            //mFont->DrawString(_(((MTGCardInstance *) target)->getName()).c_str(), x + 120, y);
        if (target->type_as_damageable == Damageable::DAMAGEABLE_MTGCARDINSTANCE)
            mFont->DrawString(_(((MTGCardInstance *) target)->getName()).c_str(), x + 35, y+15 + GetVerticalTextOffset());
        else if(target->type_as_damageable == Damageable::DAMAGEABLE_PLAYER)
            mFont->DrawString(_(((Player *) target)->getDisplayName()).c_str(), x + 35, y+15 + GetVerticalTextOffset());
    }

}

ostream& Damage::toString(ostream& out) const
{
    out << "Damage ::: target : " << target << " ; damage " << damage;
    return out;
}

DamageStack::DamageStack(GameObserver *observer)
    : GuiLayer(observer), Interruptible(observer)
{
    currentState = -1;
    type = ACTION_DAMAGES;
}

/* Damage Stack resolve process:
 1 - apply damages to targets. For each of them, send an event to the GameObserver (for Damage triggers)
 2 - Once this is done, send a "Damage Stakc Resolved" event to the GameObserver
 3 - Once that message is received on the DamageStack's side, do the "afterDamage" effects (send to graveyard, etc...)
 Using events in 2 and 3 guarantees that the "send to graveyard" effect will only apply AFTER Damaged triggers are applied
 */
int DamageStack::resolve()
{
    for (int i = (int)(mObjects.size()) - 1; i >= 0; i--)
    {
        Damage * damage = (Damage*) mObjects[i];
        if (damage->state == NOT_RESOLVED)
            damage->resolve();
    }
    ((Interruptible*)this)->getObserver()->receiveEvent(NEW WEventDamageStackResolved());
    return 1;
}

int DamageStack::receiveEvent(WEvent * e)
{
    WEventDamageStackResolved *event = dynamic_cast<WEventDamageStackResolved*> (e);
    if (!event)
        return 0;

    for (int i = (int)(mObjects.size()) - 1; i >= 0; i--)
    {
        Damage * damage = (Damage*) mObjects[i];
        if (damage->state == RESOLVED_OK)
            damage->target->afterDamage();
    }
    return 1;
}

void DamageStack::Render()
{
    float currenty = y;
    for (size_t i = 0; i < mObjects.size(); i++)
    {
        Damage * damage = (Damage*) mObjects[i];
        if (damage->state == NOT_RESOLVED)
        {
            damage->x = x;
            damage->y = currenty;
            currenty += damage->mHeight;
            damage->Render();
        }
    }
}

ostream& DamageStack::toString(ostream& out) const
{
    return (out << "DamageStack ::: currentState : " << currentState);
}


ostream& operator<<(ostream& out, const Damageable& p)
{
    out << "life=" << p.life << endl;
    out << "poisoncount=" << p.poisonCount << endl;
    out << "damagecount=" << p.damageCount << endl;
    out << "preventable=" << p.preventable << endl;
    return out;
}


bool Damageable::parseLine(const string& s)
{
    size_t limiter = s.find("=");
    if (limiter == string::npos) limiter = s.find(":");
    string areaS;
    if (limiter != string::npos)
    {
        areaS = s.substr(0, limiter);
        if (areaS.compare("life") == 0)
        {
            life = atoi((s.substr(limiter + 1)).c_str());
        }
        else if (areaS.compare("poisoncount") == 0)
        {
            poisonCount = atoi((s.substr(limiter + 1)).c_str());
        }
        else if (areaS.compare("damagecount") == 0)
        {
            damageCount = atoi((s.substr(limiter + 1)).c_str());
        }
        else if (areaS.compare("preventable") == 0)
        {
            preventable = atoi((s.substr(limiter + 1)).c_str());
        }
        else
        {
            return false;
        }
    }

    return true;
}

