_asus_fan_control_ec()
{
    local cur prev cmd word i skip
    local commands="set setp curve fan-speed fan-info temps-info help version"
    local globals="--cmd-port --data-port --gap --verbose"
    local valued="--cmd-port --data-port --gap --fan --retries --order --config --interval --hysteresis --panic-temp --sensor"

    COMPREPLY=()
    cur="${COMP_WORDS[COMP_CWORD]}"
    prev="${COMP_WORDS[COMP_CWORD-1]}"

    case "$prev" in
        --order)
            COMPREPLY=( $(compgen -W "mode-first duty-first" -- "$cur") )
            return ;;
        --sensor)
            COMPREPLY=( $(compgen -W "cpu board max" -- "$cur") )
            return ;;
        --config)
            COMPREPLY=( $(compgen -f -- "$cur") )
            return ;;
        --fan)
            COMPREPLY=( $(compgen -W "0 1" -- "$cur") )
            return ;;
        --cmd-port|--data-port|--gap|--retries|--interval|--hysteresis|--panic-temp)
            return ;;
    esac

    cmd=""
    skip=0
    for (( i=1; i < COMP_CWORD; i++ )); do
        word="${COMP_WORDS[i]}"
        if [ "$skip" = 1 ]; then
            skip=0
            continue
        fi
        case " $valued " in
            *" $word "*)
                skip=1
                continue ;;
        esac
        case " $commands " in
            *" $word "*)
                cmd="$word"
                break ;;
        esac
    done

    if [ -z "$cmd" ]; then
        COMPREPLY=( $(compgen -W "$commands $globals" -- "$cur") )
        return
    fi

    case "$cmd" in
        set|setp)
            COMPREPLY=( $(compgen -W "--fan --retries --no-verify --order $globals" -- "$cur") ) ;;
        curve)
            COMPREPLY=( $(compgen -W "--config --interval --hysteresis --panic-temp --sensor --retries --order --once --dry-run --silent $globals" -- "$cur") ) ;;
        *)
            COMPREPLY=( $(compgen -W "$globals" -- "$cur") ) ;;
    esac
}

complete -F _asus_fan_control_ec asus-fan-control-ec
